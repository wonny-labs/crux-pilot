#include <crux/gpio.h>

#include "gpio_port.h"

#include <stddef.h>

crux_status_t crux_gpio_configure_input(crux_gpio_line_t line)
{
    return crux_gpio_port_configure_input(line);
}

crux_status_t crux_gpio_read(crux_gpio_line_t line, bool *active)
{
    if (active == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    bool value;
    crux_status_t result = crux_gpio_port_read(line, &value);
    if (result == CRUX_OK) {
        *active = value;
    }
    return result;
}
