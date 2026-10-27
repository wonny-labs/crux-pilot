#include "native_logger.h"
#include "production_formats.h"
#include "production_phase.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/sys/atomic.h>

/* Extra TEST backend: count the real formatted bytes and model 115200/8N1.
 * Actual native UART remains enabled. This is not Cortex timing evidence.
 */
static size_t rendered;
static uint8_t output_buffer[128];
static atomic_t hold_next, hold_failed;
K_SEM_DEFINE(entered, 0, 1);
K_SEM_DEFINE(release_hold, 0, 1);

static int count_bytes(uint8_t *data, size_t size, void *context)
{
    ARG_UNUSED(data);
    ARG_UNUSED(context);
    rendered += size;
    return (int)size;
}
LOG_OUTPUT_DEFINE(counter, count_bytes, output_buffer, sizeof(output_buffer));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
    ARG_UNUSED(backend);
    rendered = 0;
    log_output_msg_process(&counter, &msg->log, log_backend_std_get_flags());
    if (atomic_cas(&hold_next, 1, 0)) {
        k_sem_give(&entered);
        if (k_sem_take(&release_hold, K_MSEC(100)) != 0) {
            atomic_set(&hold_failed, 1);
        }
    }
    k_sleep(K_USEC(DIV_ROUND_UP(rendered * 10U * 1000000U, 115200U)));
}
static void panic_backend(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);
}
static const struct log_backend_api api = {.process = process, .panic = panic_backend};
LOG_BACKEND_DEFINE(phase_delay, api, true);

K_THREAD_STACK_DEFINE(worker_stack, 4096);
K_THREAD_STACK_DEFINE(network_stack, 4096);
static struct k_thread worker, network;
static int64_t epoch;
static const int drift[] = {0, 1, 2, 4};

static void at(int64_t stamp)
{
    int64_t remaining = stamp - k_uptime_get();
    if (remaining > 0) {
        k_sleep(K_MSEC(remaining));
    }
}

static void run_work(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (unsigned int n = 0; n < 4; n++) {
        at(epoch + n * 1000);
        printk(M3_FMT_WORK, 1U + n * 10, (long long)(k_uptime_get() - epoch), 0, 0, 0);
    }
}

static void run_network(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (unsigned int n = 0; n < 4; n++) {
        at(epoch + 1 + n * 1001);
        long long stamp = k_uptime_get() - epoch;
        printk(M3_FMT_NET_RESOURCE, stamp, 0U, 0U, 16U, 16U, (size_t)32, (size_t)32);
        printk(M3_FMT_NET_STATE, stamp, 0, 1, 0);
    }
}

/* Setup/teardown drain only: never used to select a qualifying sample. */
static bool drain(void)
{
    int64_t end = k_uptime_get() + 2000;
    while (log_buffered_cnt() != 0 && k_uptime_get() < end) {
        k_sleep(K_MSEC(10));
    }
    return log_buffered_cnt() == 0;
}

int main(void)
{
    if (!drain()) {
        return 1;
    }
    atomic_set(&hold_next, 1);
    printk("PHASE_HELD_MESSAGE\n");
    if (k_sem_take(&entered, K_MSEC(1000)) != 0) {
        return 1;
    }
    unsigned int held = log_buffered_cnt();
    k_sem_give(&release_hold);
    k_sleep(K_MSEC(50));
    unsigned int completed = log_buffered_cnt();
    printk("PHASE_HOLD in_flight=%u completed=%u timeout=%ld\n", held, completed,
           (long)atomic_get(&hold_failed));
    if (held != 1 || completed != 0 || atomic_get(&hold_failed) != 0 || !drain()) {
        return 1;
    }
    unsigned int phase = IS_ENABLED(CRUX_TEST_UNSHIFTED) ? 0 : CRUX_M3_INITIAL_OBSERVER_PHASE_MS;
    printk("PHASE_MODEL phase_ms=%u baud=115200 bits=10 worker_priority=5 "
           "network_priority=6 main_priority=0 logger_priority=14\n",
           phase);
    if (!drain()) {
        return 1;
    }
    epoch = k_uptime_get() + 100;
    k_tid_t work = k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
                                   run_work, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_tid_t net = k_thread_create(&network, network_stack, K_THREAD_STACK_SIZEOF(network_stack),
                                  run_network, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
    for (unsigned int n = 0; n < 4; n++) {
        at(epoch + n * 1000 + phase + drift[n]);
        long long stamp = k_uptime_get() - epoch;
        unsigned int queued = log_buffered_cnt();
        struct m3_logger_resources logger = m3_logger_resources_get();
        printk(M3_FMT_OBSERVER_ALIVE, stamp, 0, 1U + n * 10, 0, 0U, (long long)0, 0, 0,
               (long long)0);
        printk(M3_FMT_STACK, 0, (size_t)4160, (size_t)4096, (size_t)1024, 0, (size_t)4096,
               (size_t)1024, 0, (size_t)4160, (size_t)4096, (size_t)1024);
        printk(M3_FMT_LOGGER, stamp, stamp, logger.found, logger.status, 2048U, logger.usable,
               logger.unused, logger.priority, queued);
        /* Bounded representative post-report analyser volume, explicitly synthetic. */
        if (n >= 2) {
            for (unsigned int thread = 0; thread < 12; thread++) {
                printk("PHASE_ANALYSER thread=%u STACK: unused 1024 usage 3072 / 4096 (75 %%); "
                       "CPU: 0 %%\n",
                       thread);
            }
        }
    }
    if (k_thread_join(work, K_MSEC(1000)) != 0 || k_thread_join(net, K_MSEC(1000)) != 0 ||
        !drain()) {
        return 1;
    }
    printk("PHASE_COMPLETE cycles=4 unshifted=%d\n", IS_ENABLED(CRUX_TEST_UNSHIFTED));
    return drain() ? 0 : 1;
}
