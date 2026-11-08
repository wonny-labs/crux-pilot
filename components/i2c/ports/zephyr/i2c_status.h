#ifndef CRUX_I2C_STATUS_H
#define CRUX_I2C_STATUS_H

#include <crux/status.h>

/* Map a native I2C driver result (0 or a negative errno) to a Crux status. */
crux_status_t crux_i2c_zephyr_status(int native_result);

#endif
