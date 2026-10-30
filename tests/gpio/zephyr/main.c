#include "consumer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static const struct gpio_dt_spec test_line = GPIO_DT_SPEC_GET(DT_ALIAS(crux_gpio0), gpios);

ZTEST(gpio, test_native_input_is_reported_as_logical_state)
{
    bool active = true;
    zassert_true(gpio_is_ready_dt(&test_line));
    zassert_equal(crux_gpio_configure_input(CRUX_GPIO_LINE_0), CRUX_OK);
    zassert_equal(gpio_emul_input_set_dt(&test_line, 1), 0);
    zassert_equal(read_logical_input(&active), CRUX_OK);
    zassert_false(active);

    zassert_equal(gpio_emul_input_set_dt(&test_line, 0), 0);
    zassert_equal(read_logical_input(&active), CRUX_OK);
    zassert_true(active);
}

ZTEST(gpio, test_argument_errors)
{
    bool active = true;
    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, NULL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_gpio_configure_input(UINT32_MAX), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(crux_gpio_configure_input(CRUX_GPIO_LINE_0), CRUX_OK);
    zassert_equal(gpio_emul_input_set_dt(&test_line, 0), 0);
    zassert_equal(read_logical_input(&active), CRUX_OK);
    zassert_true(active);
}

ZTEST_SUITE(gpio, NULL, NULL, NULL, NULL, NULL);
