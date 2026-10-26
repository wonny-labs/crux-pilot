#include <crux/thread.h>
#include <crux/time.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST(typed_fractional_sleep, test_observations_and_validation) {
    crux_monotonic_time_t now = {0}, later = {0};
    zassert_equal(crux_time_get_monotonic(NULL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_time_get_monotonic(&now), CRUX_OK);
    zassert_true(now.private_ns >= 0);
    zassert_equal(crux_time_get_monotonic(&later), CRUX_OK);
    zassert_true(crux_monotonic_is_at_or_before(now, later));
    zassert_equal(crux_thread_sleep_until((crux_monotonic_time_t){-1}), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_thread_sleep_until((crux_monotonic_time_t){0}), CRUX_OK);
    zassert_equal(crux_thread_sleep_until(later), CRUX_OK);
}

ZTEST(typed_fractional_sleep, test_exact_and_fractional_tick_deadlines) {
    BUILD_ASSERT(CONFIG_SYS_CLOCK_TICKS_PER_SEC == 128);
    crux_monotonic_time_t now;
    zassert_equal(crux_time_get_monotonic(&now), CRUX_OK);
    /* 125 ms is exactly 16 ticks; 126 ms must reach the next tick (17). */
    int64_t epoch = now.private_ns / INT64_C(125000000) + 1;
    crux_monotonic_time_t deadline = {epoch * INT64_C(125000000)};
    zassert_equal(crux_thread_sleep_until(deadline), CRUX_OK);
    zassert_true(k_uptime_ticks() >= epoch * 16);
    zassert_equal(
        crux_thread_sleep_until((crux_monotonic_time_t){deadline.private_ns + INT64_C(1000000)}),
        CRUX_OK);
    zassert_true(k_uptime_ticks() >= epoch * 16 + 1);
    crux_monotonic_time_t after;
    zassert_equal(crux_time_get_monotonic(&after), CRUX_OK);
    zassert_true(after.private_ns >= deadline.private_ns + INT64_C(1000000));
}

static crux_status_t isr_now_status, isr_sleep_status;
static crux_monotonic_time_t isr_output;
static void isr_probe(const void *unused) {
    (void)unused;
    isr_output = (crux_monotonic_time_t){73};
    isr_now_status = crux_time_get_monotonic(&isr_output);
    isr_sleep_status = crux_thread_sleep_until((crux_monotonic_time_t){0});
}
ZTEST(typed_fractional_sleep, test_isr_rejection) {
    irq_offload(isr_probe, NULL);
    zassert_equal(isr_now_status, CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(isr_output.private_ns, 73);
    zassert_equal(isr_sleep_status, CRUX_ERR_NOT_SUPPORTED);
}
ZTEST_SUITE(typed_fractional_sleep, NULL, NULL, NULL, NULL, NULL);
