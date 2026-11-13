/*
 * The execution shell of the product: Zephyr code that owns the threads and the queues and calls
 * the domain, which is plain C in ../../domain. This file also holds the product's adapters:
 * the implementation of the domain's port, and the functions that Crux services call.
 *
 * The shared-state pattern of docs/choosing-an-api.md in Crux, in this file:
 *
 *  1. One owner. Only product_owner_entry() runs domain functions.
 *  2. Reads by snapshot. The owner publishes the filtered temperature in one C11 atomic after
 *     each step. product_status_read() only loads it, on whatever thread calls it.
 *  3. Writes by message. product_command_set_limit() posts to the owner's k_msgq and never
 *     blocks. The owner applies the message between two steps.
 *  4. Locks live in the shell. This example needs none. A snapshot of several values would use a
 *     double buffer whose swap is guarded by a k_mutex defined here, never in the domain.
 *
 * Crux services call product code on other threads: a command handler on the thread of its
 * console or SSH session, a web handler on the Zephyr HTTP server thread, the reboot busy check
 * on the reboot worker, and a status read on the caller's thread. This example needs no service,
 * so main() stands in for a caller and uses the two adapters the way a handler would. A real
 * handler calls the adapter and does nothing else: no domain call, no lock.
 */
#include "../../domain/product_domain.h"
#include "../../domain/product_port.h"

#include <crux/status.h>
#include <crux/time.h>

#include <stdatomic.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* The snapshot is a C11 atomic, so a read takes no lock only if int atomics are lock-free. Zephyr's
 * own atomic_t has a C fallback that takes a global spinlock on some targets. */
BUILD_ASSERT(ATOMIC_INT_LOCK_FREE == 2, "The snapshot needs lock-free int atomics");

#define PRODUCT_STEP_MS 100
#define PRODUCT_DEFAULT_LIMIT_MK 330000
#define PRODUCT_OWNER_STACK_SIZE 1024
#define PRODUCT_OWNER_PRIORITY K_PRIO_PREEMPT(7)

enum product_command_kind {
    PRODUCT_COMMAND_SET_LIMIT,
};

/* A message to the owner. Keep it small: k_msgq copies it. */
struct product_command {
    enum product_command_kind kind;
    int32_t value;
};

/* Four messages deep. A fifth message that arrives before the owner drains the queue is refused. */
K_MSGQ_DEFINE(product_command_queue, sizeof(struct product_command), 4, 4);

/* The snapshot: written by the owner, read by any thread. */
static atomic_int product_filtered_snapshot;

/* The port of the domain. A product reads its sensor here, with a Zephyr driver. */
int product_port_read_temperature(int32_t *millikelvin)
{
    static int32_t sample = 300000;

    sample += 100;
    *millikelvin = sample;
    return 0;
}

/* Adapter for a status read: any thread, no lock, never calls the domain. */
static int32_t product_status_read(void)
{
    return atomic_load(&product_filtered_snapshot);
}

/* Adapter for a command: any thread, never blocks, never calls the domain. */
static int product_command_set_limit(int32_t limit_millikelvin)
{
    const struct product_command command = {
        .kind = PRODUCT_COMMAND_SET_LIMIT,
        .value = limit_millikelvin,
    };

    /* Return 0 when the owner will apply the command. A full queue returns -ENOMSG and the command
     * is not applied: the handler reports that to its caller, for example as "busy, try again".
     * Do not wait for room, because a handler may run on a thread that must not block. */
    return k_msgq_put(&product_command_queue, &command, K_NO_WAIT);
}

/* The owner thread: the only place that runs the domain. */
static void product_owner_entry(void *p1, void *p2, void *p3)
{
    struct product_domain domain;
    struct product_command command;

    (void)p1;
    (void)p2;
    (void)p3;
    product_domain_init(&domain, PRODUCT_DEFAULT_LIMIT_MK);
    for (;;) {
        /* Apply the messages that arrived since the last step, then step. */
        while (k_msgq_get(&product_command_queue, &command, K_NO_WAIT) == 0) {
            if (command.kind == PRODUCT_COMMAND_SET_LIMIT) {
                product_domain_set_limit(&domain, command.value);
            }
        }
        if (product_domain_step(&domain) == 0) {
            atomic_store(&product_filtered_snapshot, product_domain_filtered(&domain));
        }
        k_msleep(PRODUCT_STEP_MS);
    }
}

K_THREAD_DEFINE(product_owner, PRODUCT_OWNER_STACK_SIZE, product_owner_entry, NULL, NULL, NULL,
                PRODUCT_OWNER_PRIORITY, 0, 0);

int main(void)
{
    crux_monotonic_time_t now;
    const crux_status_t status = crux_monotonic_now(&now);

    if (status != CRUX_OK) {
        printk("product-x: clock failed, status=%d\n", (int)status);
        return -1;
    }

    printk("product-x: ready\n");

    /* Stand-ins for service callbacks: a command, then a status read. */
    const int queued = product_command_set_limit(PRODUCT_DEFAULT_LIMIT_MK + 10000);

    if (queued != 0) {
        printk("product-x: command refused, status=%d\n", queued);
    }
    k_msleep(2 * PRODUCT_STEP_MS);
    printk("product-x: filtered=%d mK\n", (int)product_status_read());
    return 0;
}
