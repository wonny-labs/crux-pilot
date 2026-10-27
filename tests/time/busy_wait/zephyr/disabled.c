#include <crux/time.h>
#include <zephyr/irq_offload.h>
#include <zephyr/ztest.h>

ZTEST(time_busy_wait_disabled, test_duration_only_contract)
{
    BUILD_ASSERT(!IS_ENABLED(CONFIG_CRUX_TIME_BUSY_WAIT));
    zassert_equal(crux_busy_wait(crux_duration_from_ns(-1)), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(0)), CRUX_OK);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(1)), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(INT64_MAX)), CRUX_ERR_NOT_SUPPORTED);
}
static void disabled_isr(const void *unused)
{
    ARG_UNUSED(unused);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(-1)), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(0)), CRUX_OK);
    zassert_equal(crux_busy_wait(crux_duration_from_ns(1)), CRUX_ERR_NOT_SUPPORTED);
}
ZTEST(time_busy_wait_disabled, test_duration_only_in_isr)
{
    irq_offload(disabled_isr, NULL);
}
ZTEST_SUITE(time_busy_wait_disabled, NULL, NULL, NULL, NULL, NULL);
