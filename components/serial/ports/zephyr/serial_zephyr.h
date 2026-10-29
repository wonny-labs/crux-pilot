#ifndef CRUX_SERIAL_ZEPHYR_H
#define CRUX_SERIAL_ZEPHYR_H
#include <crux/status.h>
#include <zephyr/device.h>
/* Private composition boundary: immutable DT binding, not a public registry. */
const struct device *crux_serial_zephyr_device(unsigned port);
#endif
