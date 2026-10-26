#include "native_logger.h"
#include "production_formats.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/atomic.h>

K_THREAD_STACK_DEFINE(worker_stack, 4096);
K_THREAD_STACK_DEFINE(network_stack, 4096);
static struct k_thread worker, network;
K_SEM_DEFINE(start, 0, 2);
static atomic_t emitted;
#define EMIT(format, ...)                                                                          \
    do {                                                                                           \
        printk(format, ##__VA_ARGS__);                                                             \
        atomic_inc(&emitted);                                                                      \
    } while (0)

/* Synthetic values exercise the real native stream, not controller functionality. */
static void resources(long long stamp)
{
    EMIT(M3_FMT_NET_RESOURCE, stamp, 0U, 0U, 16U, 16U, (size_t)32, (size_t)32);
    EMIT(M3_FMT_NET_STATE, stamp, 1, 1, 1);
}

static void run_work(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_sem_take(&start, K_FOREVER);
    for (unsigned int n = 0; n < 6; n++) {
        EMIT(M3_FMT_WORK, 100U + n * 10, (long long)(10000 + n * 1000), 0, n % 2, 0);
        k_sleep(K_MSEC(1000));
    }
}

static void run_network(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_sem_take(&start, K_FOREVER);
    for (unsigned int n = 0; n < 6; n++) {
        long long stamp = 10000 + n * 1000;
        resources(stamp);
        for (unsigned int protocol = 1; protocol <= 2; protocol++) {
            EMIT(M3_FMT_NET_EXCHANGE, 1U, protocol, n + 1, 0, stamp + protocol * 5, (long long)2);
            resources(stamp + protocol * 5);
            EMIT(M3_FMT_NET_CLEANUP, 0U, 0U, (long long)0);
            resources(stamp + protocol * 5);
        }
        EMIT(M3_FMT_NET_WORK, n + 1, stamp + 10);
        k_sleep(K_MSEC(1000));
    }
}

static void feed(unsigned int n)
{
    long long stamp = 10000 + n * 100;
    EMIT(M3_FMT_WDT_FEED, stamp, 100U + n, stamp, n / 10 + 1, stamp - n % 10 * 100);
}

static bool finish(void)
{
    int64_t deadline = k_uptime_get() + 2000;
    while (log_buffered_cnt() != 0 && k_uptime_get() < deadline) {
        k_sleep(K_MSEC(10));
    }
    k_sleep(K_MSEC(20));
    if (log_buffered_cnt() != 0) {
        printk("INTEGRATED_FIXTURE_DRAIN_FAILED\n");
        return false;
    }
    printk("INTEGRATED_FIXTURE_COMPLETE emitted=%ld expected_loss=%d\n", (long)atomic_get(&emitted),
           IS_ENABLED(CRUX_TEST_LOSS));
    k_sleep(K_MSEC(50));
    return log_buffered_cnt() == 0;
}

int main(void)
{
    EMIT(M3_FMT_BOOT, "0123456789ab", 1);
    EMIT(M3_FMT_WDT_ARMED);
#ifdef CRUX_TEST_LOSS
    /* A finite high-priority production-format burst must expose native loss. */
    for (unsigned int n = 0; n < 500; n++) {
        feed(n);
    }
    return finish() ? 0 : 1;
#endif
    k_tid_t work = k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
                                   run_work, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_tid_t net = k_thread_create(&network, network_stack, K_THREAD_STACK_SIZEOF(network_stack),
                                  run_network, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
    k_thread_name_set(work, "fixture-work");
    k_thread_name_set(net, "fixture-net");
    k_sem_give(&start);
    k_sem_give(&start);
    for (unsigned int n = 0; n < 60; n++) {
        feed(n);
        if (n % 10 == 0) {
            unsigned int queued = log_buffered_cnt();
            struct m3_logger_resources logger = m3_logger_resources_get();
            long long stamp = 10000 + n * 100;
            EMIT(M3_FMT_OBSERVER_ALIVE, stamp, 1, 100U + n, 3, n / 10 + 1, stamp, 1, 0,
                 (long long)70000);
            EMIT(M3_FMT_STACK, 0, (size_t)4160, (size_t)4096, (size_t)1024, 0, (size_t)4096,
                 (size_t)1024, 0, (size_t)4160, (size_t)4096, (size_t)1024);
            EMIT(M3_FMT_LOGGER, stamp, stamp, logger.found, logger.status, 2048U, logger.usable,
                 logger.unused, logger.priority, queued);
        }
        k_sleep(K_MSEC(100));
    }
    if (k_thread_join(work, K_MSEC(1000)) != 0 || k_thread_join(net, K_MSEC(1000)) != 0) {
        printk("INTEGRATED_FIXTURE_JOIN_FAILED\n");
        return 1;
    }
    /* Payload-only fault/boot boundaries: no watchdog is armed by this fixture. */
    EMIT(M3_FMT_FAULT_STALL, (long long)16000, 160U, (long long)15900, (long long)30900,
         (long long)70000);
    EMIT(M3_FMT_NET_FAULT_STALL, (long long)16000, 6U, (long long)15010, (long long)15900,
         (long long)30900, (long long)70000);
    EMIT(M3_FMT_BOOT, "0123456789ab", 0);
    return finish() ? 0 : 1;
}
