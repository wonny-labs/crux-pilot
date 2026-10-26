#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>

static bool drain(void)
{
    int64_t deadline = k_uptime_get() + 1000;
    while (log_buffered_cnt() != 0 && k_uptime_get() < deadline) {
        k_sleep(K_MSEC(10));
    }
    /* A dequeued message can still be in the backend. Preserve a bounded flush gap. */
    k_sleep(K_MSEC(20));
    return log_buffered_cnt() == 0;
}

int main(void)
{
#ifdef CRUX_TEST_OVERFLOW
    /* The actual native process thread is delayed; a finite burst fills its buffer. */
    for (unsigned int i = 0; i < 100; ++i) {
        printk("OVERFLOW sequence=%u payload=abcdefghijklmnopqrstuvwxyz0123456789"
               "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789\n",
               i);
    }
#else
    printk("DEFERRED_BEGIN\n");
    printk("DEFERRED_RECORD signed=%lld unsigned=%llu "
           "payload=abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789"
           "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789"
           "abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnopqrstuvwxyz0123456789\n",
           (long long)INT64_C(-9223372036854775807), (unsigned long long)UINT64_MAX);
#endif
    if (!drain()) {
        printk("DEFERRED_DRAIN_FAILED\n");
        return 1;
    }
#ifdef CRUX_TEST_OVERFLOW
    printk("OVERFLOW_COMPLETE\n");
#else
    printk("DEFERRED_COMPLETE\n");
#endif
    return drain() ? 0 : 1;
}
