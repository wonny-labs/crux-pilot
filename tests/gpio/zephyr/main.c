#include "consumer.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

static const struct gpio_dt_spec test_line = GPIO_DT_SPEC_GET(DT_ALIAS(crux_gpio0), gpios);

ZTEST(gpio, test_native_input_is_reported_as_logical_state)
{
    bool active = true;
    zassert_true(gpio_is_ready_dt(&test_line));
    zassert_equal(gpio_emul_input_set_dt(&test_line, 1), 0);
    zassert_equal(gpio_consumer_read_input(&active), CRUX_OK);
    zassert_false(active);

    zassert_equal(gpio_emul_input_set_dt(&test_line, 0), 0);
    zassert_equal(gpio_consumer_read_input(&active), CRUX_OK);
    zassert_true(active);
}

ZTEST(gpio, test_direction_and_argument_errors)
{
    bool active = true;
    zassert_equal(crux_gpio_configure(CRUX_GPIO_LINE_0, (crux_gpio_direction_t)99),
                  CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, NULL), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_gpio_configure(UINT32_MAX, CRUX_GPIO_INPUT), CRUX_ERR_OUT_OF_RANGE);
    zassert_equal(crux_gpio_configure(CRUX_GPIO_LINE_0, CRUX_GPIO_OUTPUT), CRUX_OK);
    zassert_equal(gpio_emul_output_get_dt(&test_line), 1);
    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, &active), CRUX_ERR_NOT_SUPPORTED);
    zassert_true(active);
    zassert_equal(crux_gpio_write(CRUX_GPIO_LINE_0, true), CRUX_OK);
    zassert_equal(gpio_emul_output_get_dt(&test_line), 0);
}

ZTEST_SUITE(gpio, NULL, NULL, NULL, NULL, NULL);
