#ifndef CRUX_GPIO_HOST_TEST_H
#define CRUX_GPIO_HOST_TEST_H

#include <crux/gpio.h>

crux_status_t crux_gpio_host_test_drive(crux_gpio_line_t line, bool active);
crux_status_t crux_gpio_host_test_output(crux_gpio_line_t line, bool *active);
void crux_gpio_host_test_fail_reads(bool fail);

#endif
