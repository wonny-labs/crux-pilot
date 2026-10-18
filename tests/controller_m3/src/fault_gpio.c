/* This native test fixture supplies an error hook missing from gpio_emul.
 * It is compiled only in tests; no production driver or test control is added. */
#include "fault_gpio.h"

#include <errno.h>
#include <zephyr/drivers/gpio.h>

#if DT_NODE_HAS_STATUS(DT_NODELABEL(m3_fault_gpio), okay)
struct fault_gpio_data {
    struct gpio_driver_data common;
    int raw;
    bool fail_read;
};
static struct fault_gpio_data data = {.raw = 1};
static const struct gpio_driver_config configuration = {.port_pin_mask = BIT(0)};

static int configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags) {
    ARG_UNUSED(port);
    if (pin != 0 || !(flags & GPIO_INPUT) || (flags & GPIO_OUTPUT)) {
        return -ENOTSUP;
    }
    return 0;
}

static int read_raw(const struct device *port, gpio_port_value_t *value) {
    struct fault_gpio_data *state = port->data;
    if (state->fail_read) {
        return -EIO;
    }
    *value = (gpio_port_value_t)state->raw;
    return 0;
}

static DEVICE_API(gpio, fault_api) = {.pin_configure = configure, .port_get_raw = read_raw};
DEVICE_DT_DEFINE(DT_NODELABEL(m3_fault_gpio), NULL, NULL, &data, &configuration, POST_KERNEL,
                 CONFIG_GPIO_INIT_PRIORITY, &fault_api);

void m3_test_gpio_value(int raw) {
    data.raw = raw;
}
void m3_test_gpio_fail_read(bool fail) {
    data.fail_read = fail;
}
#endif
