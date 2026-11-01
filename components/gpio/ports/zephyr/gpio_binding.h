#ifndef CRUX_GPIO_BINDING_H
#define CRUX_GPIO_BINDING_H

#include <crux/gpio.h>
#include <zephyr/drivers/gpio.h>

const struct gpio_dt_spec *crux_gpio_zephyr_spec(crux_gpio_line_t line);

#endif
