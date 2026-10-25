#include "crux_time_command.h"
#include "crux_time_test.h"
#include "time_port_internal.h"
#include "time_rtc_zephyr.h"
#include <crux/time_authority.h>
#include <errno.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/sys/clock.h>
#include <zephyr/ztest.h>
#if defined(CRUX_TIME_FAKE_RTC)
#include "fake_rtc.h"
#endif

#if DT_HAS_ALIAS(rtc)
#if DT_NODE_HAS_STATUS(DT_ALIAS(rtc), okay)
#define CRUX_TEST_HAS_RTC 1
#endif
#endif

static void assert_shell_result(const char *command, const char *expected) {
    const struct shell *shell = shell_backend_dummy_get_ptr();
    size_t output_size = 0;
    shell_backend_dummy_clear_output(shell);
    zassert_equal(shell_execute_cmd(shell, command), 0);
    const char *output = shell_backend_dummy_get_output(shell, &output_size);
    zassert_true(output_size > 0);
    zassert_not_null(strstr(output, expected));
}
static void *setup(void) {
    k_usleep(10);
    return NULL;
}
ZTEST_SUITE(crux_time_zephyr, NULL, setup, NULL, NULL, NULL);

ZTEST(crux_time_zephyr, test_native_rtc_error_status_mapping) {
    zassert_equal(crux_time_zephyr_status_from_rtc_error(0), CRUX_OK);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ENODATA), CRUX_ERR_NO_DATA);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-EINVAL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ERANGE), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ENODEV), CRUX_ERR_NOT_READY);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ENXIO), CRUX_ERR_NOT_READY);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-EIO), CRUX_ERR_IO);
}

ZTEST(crux_time_zephyr, test_system_realtime_and_shared_command_without_rtc_dependency) {
    crux_time_test_reset();
    zassert_equal(crux_time_init(), CRUX_OK);
    crux_realtime_t instant = {123456789, 73};
    zassert_equal(crux_time_get_realtime(&instant), CRUX_ERR_NO_DATA);
    zassert_equal(instant.seconds, 123456789);
    zassert_equal(instant.nanoseconds, 73);
    crux_realtime_snapshot_t snapshot;
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_false(snapshot.is_time_valid);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_NONE);
    zassert_equal(snapshot.timestamp.seconds, 0);
    zassert_equal(snapshot.timestamp.nanoseconds, 0);
    assert_shell_result("time set 2024-01-01T00:00:00 extra", "ERR INVALID_ARGUMENT");
    const crux_time_command_result_t invalid_date =
        crux_time_command_execute("time set 2023-02-29T12:00:00");
    zassert_equal(invalid_date.status, CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_time_get_realtime(&instant), CRUX_ERR_NO_DATA);
    assert_shell_result("time set 2038-01-19T03:14:08", "OK");
    assert_shell_result("time get", "2038-01-19T03:14:08 UTC");
    zassert_equal(crux_time_init(), CRUX_OK);
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_MANUAL);
    zassert_true(snapshot.is_time_valid);
    zassert_false(snapshot.is_actively_synchronized);
    zassert_true(snapshot.timestamp.seconds >= INT64_C(2147483648));
}

ZTEST(crux_time_zephyr, test_fractional_system_clock_authority_and_explicit_adoption) {
    crux_time_test_reset();
    zassert_equal(crux_time_init(), CRUX_OK);
    const crux_realtime_t value = {INT64_C(1792238400), 123456789};
    zassert_equal(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_EXTERNAL, true), CRUX_OK);
    crux_realtime_snapshot_t snapshot;
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_EXTERNAL);
    zassert_true(snapshot.is_actively_synchronized);
    zassert_equal(snapshot.timestamp.seconds, value.seconds);
    zassert_true(snapshot.timestamp.nanoseconds >= value.nanoseconds);
    zassert_equal(crux_time_notify_sync_state(false), CRUX_OK);
    zassert_equal(crux_time_invalidate_realtime(), CRUX_OK);
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_false(snapshot.is_time_valid);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_EXTERNAL);
    zassert_equal(crux_time_adopt_realtime(), CRUX_OK);
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_true(snapshot.is_time_valid);
    zassert_false(snapshot.is_actively_synchronized);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_UNKNOWN);
    zassert_equal(crux_time_notify_sync_state(true), CRUX_ERR_INVALID_STATE);
}

static void authority_in_isr(const void *unused) {
    ARG_UNUSED(unused);
    crux_realtime_t out = {42, 73};
    const crux_realtime_t value = {INT64_C(1792238400), 0};
    crux_realtime_snapshot_t snapshot = {.timestamp = {42, 73}};
    zassert_equal(crux_time_init(), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_get_realtime(&out), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_MANUAL, false),
                  CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_adopt_realtime(), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_notify_sync_state(false), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_invalidate_realtime(), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_zephyr_rtc_read(&out), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_time_zephyr_rtc_store(&value), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(out.seconds, 42);
    zassert_equal(out.nanoseconds, 73);
    zassert_equal(snapshot.timestamp.seconds, 42);
    zassert_equal(crux_time_get_realtime(NULL), CRUX_ERR_INVALID_ARGUMENT);
}
ZTEST(crux_time_zephyr, test_authority_rejects_isr_without_mutation) {
    irq_offload(authority_in_isr, NULL);
}

K_THREAD_STACK_DEFINE(writer_stack, 2048);
static struct k_thread writer_thread;
static void concurrent_writer(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    for (unsigned i = 0; i < 1000; ++i) {
        const bool external = (i % 2) != 0;
        const crux_realtime_t value = {external ? INT64_C(1792238400) : INT64_C(1700000000), 0};
        zassert_equal(
            crux_time_set_realtime(
                &value, external ? CRUX_TIME_SOURCE_EXTERNAL : CRUX_TIME_SOURCE_MANUAL, external),
            CRUX_OK);
        k_yield();
    }
}
ZTEST(crux_time_zephyr, test_concurrent_native_snapshots_match_authority) {
    crux_time_test_reset();
    zassert_equal(crux_time_init(), CRUX_OK);
    const crux_realtime_t initial = {INT64_C(1700000000), 0};
    zassert_equal(crux_time_set_realtime(&initial, CRUX_TIME_SOURCE_MANUAL, false), CRUX_OK);
    k_tid_t worker = k_thread_create(
        &writer_thread, writer_stack, K_THREAD_STACK_SIZEOF(writer_stack), concurrent_writer, NULL,
        NULL, NULL, k_thread_priority_get(k_current_get()), 0, K_NO_WAIT);
    for (unsigned i = 0; i < 1000; ++i) {
        crux_realtime_snapshot_t snapshot;
        zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
        const bool external = snapshot.last_source == CRUX_TIME_SOURCE_EXTERNAL;
        const int64_t expected = external ? INT64_C(1792238400) : INT64_C(1700000000);
        zassert_true(snapshot.is_time_valid);
        zassert_equal(snapshot.is_actively_synchronized, external);
        zassert_true(snapshot.timestamp.seconds >= expected &&
                     snapshot.timestamp.seconds < expected + 5);
        k_yield();
    }
    zassert_equal(k_thread_join(worker, K_SECONDS(5)), 0);
}

ZTEST(crux_time_zephyr, test_explicit_rtc_storage_never_controls_system_clock_validity) {
    crux_time_test_reset();
    zassert_equal(crux_time_init(), CRUX_OK);
    const crux_realtime_t rtc_value = {INT64_C(1700000000), 0};
    const crux_realtime_t system_value = {INT64_C(1792238400), 123456789};
    zassert_equal(crux_time_set_realtime(&system_value, CRUX_TIME_SOURCE_MANUAL, false), CRUX_OK);
    crux_realtime_t loaded = {42, 73};
#if defined(CRUX_TEST_HAS_RTC)
    zassert_equal(crux_time_zephyr_rtc_store(&rtc_value), CRUX_OK);
    zassert_equal(crux_time_zephyr_rtc_read(&loaded), CRUX_OK);
    zassert_true(loaded.seconds >= rtc_value.seconds && loaded.seconds <= rtc_value.seconds + 1);
    zassert_equal(loaded.nanoseconds, 0);
    zassert_equal(crux_time_zephyr_rtc_store(&system_value), CRUX_ERR_NOT_SUPPORTED);
#else
    zassert_equal(crux_time_zephyr_rtc_store(&rtc_value), CRUX_ERR_NOT_READY);
    zassert_equal(crux_time_zephyr_rtc_read(&loaded), CRUX_ERR_NOT_READY);
    zassert_equal(loaded.seconds, 42);
    zassert_equal(loaded.nanoseconds, 73);
#endif
    crux_realtime_snapshot_t snapshot;
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_true(snapshot.is_time_valid);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_MANUAL);
    zassert_true(snapshot.timestamp.seconds >= system_value.seconds);
    zassert_true(snapshot.timestamp.seconds <= system_value.seconds + 1);
#if defined(CRUX_TEST_HAS_RTC)
    /* Loading is explicit application policy, not an automatic Time init step. */
    zassert_equal(crux_time_set_realtime(&loaded, CRUX_TIME_SOURCE_RTC, false), CRUX_OK);
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_RTC);
#endif
}

#if defined(CRUX_TIME_FAKE_RTC)
ZTEST(crux_time_zephyr, test_rtc_native_faults_preserve_outputs_and_system_validity) {
    crux_time_test_reset();
    zassert_equal(crux_time_init(), CRUX_OK);
    const crux_realtime_t value = {INT64_C(1792238400), 0};
    zassert_equal(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_EXTERNAL, true), CRUX_OK);
    const int native_errors[] = {-ENODATA, -EINVAL, -ERANGE, -ENODEV, -ENXIO, -EIO};
    const crux_status_t statuses[] = {CRUX_ERR_NO_DATA,      CRUX_ERR_INVALID_ARGUMENT,
                                      CRUX_ERR_OUT_OF_RANGE, CRUX_ERR_NOT_READY,
                                      CRUX_ERR_NOT_READY,    CRUX_ERR_IO};
    for (size_t i = 0; i < ARRAY_SIZE(native_errors); ++i) {
        fake_rtc_errors(native_errors[i], 0);
        crux_realtime_t out = {42, 73};
        zassert_equal(crux_time_zephyr_rtc_read(&out), statuses[i]);
        zassert_equal(out.seconds, 42);
        zassert_equal(out.nanoseconds, 73);
    }
    fake_rtc_errors(0, -EIO);
    zassert_equal(crux_time_zephyr_rtc_store(&value), CRUX_ERR_IO);
    crux_realtime_snapshot_t snapshot;
    zassert_equal(crux_time_get_realtime_snapshot(&snapshot), CRUX_OK);
    zassert_true(snapshot.is_time_valid);
    zassert_true(snapshot.is_actively_synchronized);
    zassert_equal(snapshot.last_source, CRUX_TIME_SOURCE_EXTERNAL);
    fake_rtc_errors(0, 0);
    const struct rtc_time bad[] = {{.tm_year = INT32_MAX, .tm_mon = 0, .tm_mday = 1},
                                   {.tm_year = 123, .tm_mon = 12, .tm_mday = 1},
                                   {.tm_year = 123, .tm_mon = 0, .tm_mday = 1, .tm_nsec = -1}};
    for (size_t i = 0; i < ARRAY_SIZE(bad); ++i) {
        fake_rtc_value(bad[i]);
        crux_realtime_t out = {42, 73};
        zassert_equal(crux_time_zephyr_rtc_read(&out),
                      i == 0 ? CRUX_ERR_OUT_OF_RANGE : CRUX_ERR_INVALID_ARGUMENT);
        zassert_equal(out.seconds, 42);
        zassert_equal(out.nanoseconds, 73);
    }
}
#endif
