#include <crux/time.h>

#include "crux_time_command.h"
#include "crux_time_test.h"
#include "time_port.h"
#include "time_port_internal.h"

#include <errno.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#include <string.h>

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

static void *crux_time_zephyr_setup(void) {
    k_usleep(10);
    return NULL;
}

ZTEST_SUITE(crux_time_zephyr, NULL, crux_time_zephyr_setup, NULL, NULL, NULL);

ZTEST(crux_time_zephyr, test_native_rtc_error_status_mapping) {
    zassert_equal(crux_time_zephyr_status_from_rtc_error(0), CRUX_OK);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ENODATA), CRUX_ERR_NO_DATA);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-EINVAL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ERANGE), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ENODEV), CRUX_ERR_NOT_READY);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-ENXIO), CRUX_ERR_NOT_READY);
    zassert_equal(crux_time_zephyr_status_from_rtc_error(-EIO), CRUX_ERR_IO);
}

#if defined(CRUX_TEST_HAS_RTC)
ZTEST(crux_time_zephyr, test_rtc_backend_and_shared_command_contract) {
    crux_time_test_reset();

    zassert_equal(crux_time_init(), CRUX_OK);

    crux_time_instant_t instant = INT64_C(123456789);
    zassert_equal(crux_time_port_read(&instant), CRUX_ERR_NO_DATA);
    zassert_equal(instant, INT64_C(123456789));
    zassert_equal(crux_time_get(&instant), CRUX_ERR_NO_DATA);

    assert_shell_result("time set 2024-01-01T00:00:00 extra", "ERR INVALID_ARGUMENT");
    zassert_equal(crux_time_port_read(&instant), CRUX_ERR_NO_DATA);

    const crux_time_command_result_t invalid_date =
        crux_time_command_execute("time set 2023-02-29T12:00:00");
    const crux_time_command_result_t trailing_input =
        crux_time_command_execute("time set 2024-01-01T00:00:00 extra");
    zassert_equal(invalid_date.status, CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(trailing_input.status, CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_time_port_read(&instant), CRUX_ERR_NO_DATA);

    const crux_time_command_result_t set_result =
        crux_time_command_execute("time set 2038-01-19T03:14:08");
    const crux_time_command_result_t get_result = crux_time_command_execute("time get");
    zassert_equal(set_result.status, CRUX_OK);
    zassert_true(strcmp(set_result.text, "OK") == 0);
    zassert_equal(get_result.status, CRUX_OK);
    zassert_true(strcmp(get_result.text, "2038-01-19T03:14:08 UTC") == 0);
}
#else
ZTEST(crux_time_zephyr, test_missing_rtc_reports_not_ready) {
    crux_time_test_reset();

    crux_time_instant_t instant = INT64_C(123456789);
    zassert_equal(crux_time_init(), CRUX_ERR_NOT_READY);
    zassert_equal(crux_time_get(&instant), CRUX_ERR_NOT_READY);
    zassert_equal(instant, INT64_C(123456789));

    assert_shell_result("time set 2024-01-01T00:00:00 extra", "ERR INVALID_ARGUMENT");
}
#endif
