#include "production_formats.h"

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>

static const long long stamp = INT64_C(9223372036854625807);
static unsigned int emitted;

static void resources(void)
{
    printk(M3_FMT_RESOURCE, stamp, 0U, 0U, 16U, 16U, (size_t)32, (size_t)32);
    printk(M3_FMT_STATE, stamp, 1, 1, 0);
    emitted += 2;
}

static void cleanup(void)
{
    printk(M3_FMT_CLEANUP_OK, 0U, 0U, (long long)5000);
    emitted++;
}

int main(void)
{
    /* A finite conservative production-format volume, not network/WCET proof. */
    printk(M3_FMT_REQUESTED);
    resources();
    printk(M3_FMT_ROUND_BUDGET, 7U, stamp, stamp + 150000);
    cleanup();
    emitted += 2;
    for (unsigned int protocol = 1; protocol <= 2; protocol++) {
        for (unsigned int sequence = 1; sequence <= 10; sequence++) {
            printk(M3_FMT_EXCHANGE, 7U, protocol, sequence, -116, stamp, (long long)2000);
            emitted++;
            resources();
            cleanup();
            resources();
#ifdef CRUX_TEST_PACED
            /* Contrasting schedule only; production has no mandatory minimum delay. */
            k_sleep(K_MSEC(50));
#endif
        }
    }
    printk(M3_FMT_ROUND, 7U, -5, stamp);
    resources();
    printk(M3_FMT_STACK, 0, (size_t)4096, (size_t)1024);
    printk(M3_FMT_LOGGER, stamp, stamp, 1U, 0, 2048U, (size_t)2048, (size_t)1024, 14, 0U);
    emitted += 3;
    uint32_t capacity = 0, queued_bytes = 0;
    int usage_status = log_mem_get_usage(&capacity, &queued_bytes);
    int64_t deadline = k_uptime_get() + 2000;
    while (log_buffered_cnt() != 0 && k_uptime_get() < deadline) {
        k_sleep(K_MSEC(10));
    }
    k_sleep(K_MSEC(20));
    if (log_buffered_cnt() != 0) {
        printk("NETWORK_BURST_DRAIN_FAILED\n");
        return 1;
    }
    printk("NETWORK_BURST_COMPLETE emitted=%u capacity=%u queued_bytes=%u usage_status=%d\n",
           emitted, capacity, queued_bytes, usage_status);
    k_sleep(K_MSEC(50));
    return 0;
}
