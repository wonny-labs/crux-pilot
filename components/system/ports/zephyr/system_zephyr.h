#ifndef CRUX_SYSTEM_ZEPHYR_H
#define CRUX_SYSTEM_ZEPHYR_H
#include <stdbool.h>
#include <zephyr/device.h>
/* Composition-owned binding, not a public device or watchdog interface. */
const struct device *crux_watchdog_zephyr_device(void);
bool crux_watchdog_zephyr_running(void);
#endif
