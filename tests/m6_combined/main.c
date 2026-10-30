#include <crux/gpio.h>
#include <crux/i2c.h>
#include <crux/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/ztest.h>

ZTEST(m6_combined, test_capabilities_coexist)
{
    const struct gpio_dt_spec line = GPIO_DT_SPEC_GET(DT_ALIAS(crux_gpio0), gpios);
    const uint8_t write[] = {0x10, 0xa5};
    const uint8_t reg = 0x10;
    uint8_t value = 0;
    const uint8_t tx[] = {0x12, 0x34};
    uint8_t rx[sizeof(tx)] = {0};
    bool active = false;

    zassert_equal(crux_gpio_configure_input(CRUX_GPIO_LINE_0), CRUX_OK);
    zassert_equal(gpio_emul_input_set_dt(&line, 0), 0);
    zassert_equal(crux_gpio_read(CRUX_GPIO_LINE_0, &active), CRUX_OK);
    zassert_true(active);
    zassert_equal(crux_i2c_transfer(0x32, write, sizeof(write), NULL, 0), CRUX_OK);
    zassert_equal(crux_i2c_transfer(0x32, &reg, 1, &value, 1), CRUX_OK);
    zassert_equal(value, 0xa5);
    zassert_equal(crux_spi_transceive(tx, rx, sizeof(tx)), CRUX_OK);
    zassert_mem_equal(tx, rx, sizeof(tx));
}

ZTEST_SUITE(m6_combined, NULL, NULL, NULL, NULL, NULL);
