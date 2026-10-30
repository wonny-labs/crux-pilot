#ifndef CRUX_GPIO_PORT_H
#define CRUX_GPIO_PORT_H

#include <crux/gpio.h>

crux_status_t crux_gpio_port_configure(crux_gpio_line_t line, crux_gpio_direction_t direction);
crux_status_t crux_gpio_port_read(crux_gpio_line_t line, bool *active);
crux_status_t crux_gpio_port_write(crux_gpio_line_t line, bool active);

#endif
