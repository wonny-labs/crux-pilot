#include "gpio_binding.h"

#include "gpio_port.h"
#include "gpio_status.h"

#include <zephyr/kernel.h>

static bool configured;

static crux_status_t selected_line(crux_gpio_line_t line, const struct gpio_dt_spec **out)
{
    if (k_is_in_isr()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    const struct gpio_dt_spec *spec = crux_gpio_zephyr_spec(line);
    if (spec == NULL) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    if (!gpio_is_ready_dt(spec)) {
        return CRUX_ERR_NOT_READY;
    }
    *out = spec;
    return CRUX_OK;
}

crux_status_t crux_gpio_port_configure_input(crux_gpio_line_t line)
{
    const struct gpio_dt_spec *spec;
    crux_status_t result = selected_line(line, &spec);
    if (result != CRUX_OK) {
        return result;
    }
    int native = gpio_pin_configure_dt(spec, GPIO_INPUT);
    if (native == 0) {
        configured = true;
    }
    return crux_gpio_zephyr_status(native);
}

crux_status_t crux_gpio_port_read(crux_gpio_line_t line, bool *active)
{
    const struct gpio_dt_spec *spec;
    crux_status_t result = selected_line(line, &spec);
    if (result != CRUX_OK) {
        return result;
    }
    if (!configured) {
        return CRUX_ERR_NOT_READY;
    }
    int native = gpio_pin_get_dt(spec);
    if (native < 0) {
        return crux_gpio_zephyr_status(native);
    }
    *active = native != 0;
    return CRUX_OK;
}
