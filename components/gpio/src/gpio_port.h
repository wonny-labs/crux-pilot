#ifndef CRUX_GPIO_PORT_H
#define CRUX_GPIO_PORT_H

#include <crux/gpio.h>

crux_status_t crux_gpio_port_configure_input(crux_gpio_line_t line);
crux_status_t crux_gpio_port_read(crux_gpio_line_t line, bool *active);

#endif
