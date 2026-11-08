#include <crux/gpio.h>
#include <zephyr/ztest.h>

ZTEST(gpio_open_drain, test_unsupported_native_result_and_unconfigured_line)
{
    bool active = true;

    zassert_equal(crux_gpio_configure_input(CRUX_GPIO_LINE_0), CRUX_ERR_NOT_SUPPORTED);
    /* A failed configuration must not leave the line readable. */
    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, &active), CRUX_ERR_NOT_READY);
    zassert_true(active, "the output must be unchanged on error");
}

ZTEST_SUITE(gpio_open_drain, NULL, NULL, NULL, NULL, NULL);
