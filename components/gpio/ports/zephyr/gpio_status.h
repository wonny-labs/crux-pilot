#ifndef CRUX_GPIO_STATUS_H
#define CRUX_GPIO_STATUS_H

#include <crux/status.h>

/* Map a native GPIO driver result (0 or a negative errno) to a Crux status. */
crux_status_t crux_gpio_zephyr_status(int native_result);

#endif
