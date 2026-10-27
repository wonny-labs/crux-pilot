#include <crux/time.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

ZTEST(delay_native, test_limit_and_native_rounding_without_realtime_init)
{
    BUILD_ASSERT(!IS_ENABLED(CONFIG_RTC));
    const int64_t ns =
        sys_clock_hw_cycles_per_sec() == 1000000 ? INT64_C(2147483647000) : INT64_C(3579139000);
    zassert_true(sys_clock_hw_cycles_per_sec() == 1000000 ||
                 sys_clock_hw_cycles_per_sec() == 600000000);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(0)), CRUX_OK);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(-1)), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(ns + 1)), CRUX_ERR_OUT_OF_RANGE);
    const uint32_t start = k_cycle_get_32();
    zassert_equal(crux_busy_wait(crux_duration_from_ns(1001)), CRUX_OK);
    const uint32_t elapsed = k_cycle_get_32() - start;
    zassert_true(elapsed >= k_us_to_cyc_ceil32(2));
}

static void normal_isr(const void *unused)
{
    ARG_UNUSED(unused);
    zassert_true(k_is_in_isr());
    zassert_equal(crux_busy_wait(crux_duration_from_ns(1)), CRUX_OK);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(-1)), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(INT64_MAX)), CRUX_ERR_OUT_OF_RANGE);
}
ZTEST(delay_native, test_selected_normal_isr_context)
{
    irq_offload(normal_isr, NULL);
}

static atomic_t in_wait, irq_seen;
static uint32_t irq_cycle;
static struct k_timer irq_timer;
static void observe_irq(struct k_timer *timer)
{
    ARG_UNUSED(timer);
    if (atomic_get(&in_wait)) {
        irq_cycle = k_cycle_get_32();
        atomic_set(&irq_seen, 1);
    }
}
ZTEST(delay_native, test_thread_wait_allows_irq_before_its_deadline)
{
    atomic_clear(&irq_seen);
    k_timer_init(&irq_timer, observe_irq, NULL);
    const uint32_t start = k_cycle_get_32();
    k_timer_start(&irq_timer, K_MSEC(1), K_NO_WAIT);
    atomic_set(&in_wait, 1);
    const crux_status_t status = crux_busy_wait(crux_duration_from_ms(5));
    atomic_clear(&in_wait);
    k_timer_stop(&irq_timer);
    zassert_equal(status, CRUX_OK);
    zassert_true(atomic_get(&irq_seen), "native IRQ did not execute within thread wait");
    zassert_true(irq_cycle - start < k_us_to_cyc_ceil32(5000),
                 "interrupts were deferred until the requested delay ended");
}

ZTEST(delay_native, test_preserves_already_masked_irq_state)
{
    unsigned int key = irq_lock();
    crux_status_t status = crux_busy_wait(crux_duration_from_ns(1));
    unsigned int after = irq_lock();
    irq_unlock(after);
    irq_unlock(key);
    zassert_equal(status, CRUX_OK);
    zassert_false(arch_irq_unlocked(after));
}

ZTEST_SUITE(delay_native, NULL, NULL, NULL, NULL, NULL);
