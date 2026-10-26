#include <crux/thread.h>
#include <crux/time.h>
#include <crux/time_authority.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST(typed_time_sleep, test_monotonic_without_rtc_or_init) {
    BUILD_ASSERT(!IS_ENABLED(CONFIG_RTC));
    crux_monotonic_time_t before, after;
    zassert_equal(crux_time_get_monotonic(&before), CRUX_OK);
    zassert_equal(crux_time_get_monotonic(&after), CRUX_OK);
    zassert_true(crux_monotonic_is_at_or_before(before, after));
}

ZTEST(typed_time_sleep, test_realtime_without_rtc_driver) {
    BUILD_ASSERT(!IS_ENABLED(CONFIG_RTC));
    zassert_equal(crux_time_init(), CRUX_OK);
    crux_realtime_snapshot_t snapshot;
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_false(snapshot.is_time_valid);
    const crux_realtime_t value = {INT64_C(1792238400), 123456789};
    zassert_equal(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_MANUAL, false), CRUX_OK);
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_true(snapshot.is_time_valid);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_MANUAL);
    zassert_equal(snapshot.timestamp.seconds, value.seconds);
    zassert_true(snapshot.timestamp.nanoseconds >= value.nanoseconds);
}

ZTEST(typed_time_sleep, test_deadline_uses_next_tick_upper_bound) {
    bool checked = false;
    for (int i = 0; i < 128 && !checked; ++i) {
        int64_t before = k_uptime_ticks();
        crux_monotonic_time_t deadline;
        zassert_equal(crux_time_make_deadline_after(crux_duration_from_ns(1), &deadline), CRUX_OK);
        int64_t after = k_uptime_ticks();
        if (before == after) {
            zassert_equal(deadline.private_ns, (before + 1) * INT64_C(1000000) + 1);
            checked = true;
        }
    }
    zassert_true(checked, "could not observe a stable native clock tick");
}

ZTEST(typed_time_sleep, test_absolute_and_relative_waits) {
    crux_monotonic_time_t deadline, after, before;
    zassert_equal(crux_time_make_deadline_after(crux_duration_from_ms(2), &deadline), CRUX_OK);
    zassert_equal(crux_thread_sleep_until(deadline), CRUX_OK);
    zassert_equal(crux_time_get_monotonic(&after), CRUX_OK);
    zassert_true(crux_monotonic_is_at_or_before(deadline, after));
    zassert_equal(crux_thread_sleep_until(deadline), CRUX_OK);
    zassert_equal(crux_time_get_monotonic(&before), CRUX_OK);
    zassert_equal(crux_thread_sleep_for(crux_duration_from_ms(2)), CRUX_OK);
    zassert_equal(crux_time_get_monotonic(&after), CRUX_OK);
    zassert_true(crux_monotonic_diff(after, before).private_ns >= INT64_C(2000000));
}

static void isr_probe(const void *argument) {
    (void)argument;
    crux_monotonic_time_t out = {73};
    zassert_equal(crux_time_get_monotonic(&out), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(out.private_ns, 73);
    zassert_equal(crux_time_make_deadline_after(crux_duration_from_ns(1), &out),
                  CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_thread_sleep_for(crux_duration_from_ns(0)), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_thread_sleep_until(out), CRUX_ERR_NOT_SUPPORTED);
}
ZTEST(typed_time_sleep, test_isr_rejection_preserves_outputs) {
    irq_offload(isr_probe, NULL);
}

ZTEST_SUITE(typed_time_sleep, NULL, NULL, NULL, NULL, NULL);
