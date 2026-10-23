#include <crux/monotonic.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST(monotonic, test_observations_and_validation) {
    int64_t now = -1, later = -1;
    zassert_equal(crux_monotonic_now(NULL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_monotonic_now(&now), CRUX_OK);
    zassert_true(now >= 0);
    zassert_equal(crux_monotonic_now(&later), CRUX_OK);
    zassert_true(later >= now);
    zassert_equal(crux_monotonic_sleep_until(-1), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_monotonic_sleep_until(0), CRUX_OK);
    zassert_equal(crux_monotonic_sleep_until(later), CRUX_OK);
    zassert_equal(crux_monotonic_sleep_until(INT64_MAX), CRUX_ERR_OUT_OF_RANGE);
}

ZTEST(monotonic, test_exact_and_fractional_tick_deadlines) {
    BUILD_ASSERT(CONFIG_SYS_CLOCK_TICKS_PER_SEC == 128);
    int64_t now;
    zassert_equal(crux_monotonic_now(&now), CRUX_OK);
    /* 125 ms is exactly 16 ticks; 126 ms must reach the next tick (17). */
    int64_t epoch = now / 125 + 1;
    int64_t deadline = epoch * 125;
    zassert_equal(crux_monotonic_sleep_until(deadline), CRUX_OK);
    zassert_true(k_uptime_ticks() >= epoch * 16);
    zassert_equal(crux_monotonic_sleep_until(deadline + 1), CRUX_OK);
    zassert_true(k_uptime_ticks() >= epoch * 16 + 1);
    int64_t after;
    zassert_equal(crux_monotonic_now(&after), CRUX_OK);
    zassert_true(after >= deadline + 1);
}

static crux_status_t isr_now_status, isr_sleep_status;
static int64_t isr_output;
static void isr_probe(const void *unused) {
    (void)unused;
    isr_output = 73;
    isr_now_status = crux_monotonic_now(&isr_output);
    isr_sleep_status = crux_monotonic_sleep_until(0);
}
ZTEST(monotonic, test_isr_rejection) {
    irq_offload(isr_probe, NULL);
    zassert_equal(isr_now_status, CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(isr_output, 73);
    zassert_equal(isr_sleep_status, CRUX_ERR_NOT_SUPPORTED);
}
ZTEST_SUITE(monotonic, NULL, NULL, NULL, NULL, NULL);
