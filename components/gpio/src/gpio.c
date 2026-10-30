#include <crux/gpio.h>

#include "gpio_port.h"

#include <stddef.h>

crux_status_t crux_gpio_configure(crux_gpio_line_t line, crux_gpio_direction_t direction)
{
    if (direction != CRUX_GPIO_INPUT && direction != CRUX_GPIO_OUTPUT) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    return crux_gpio_port_configure(line, direction);
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

crux_status_t crux_gpio_write(crux_gpio_line_t line, bool active)
{
    return crux_gpio_port_write(line, active);
}
