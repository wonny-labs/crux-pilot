#ifndef CRUX_SYSTEM_ZEPHYR_H
#define CRUX_SYSTEM_ZEPHYR_H
#include <stdbool.h>
#include <zephyr/device.h>
/* Composition-owned binding, not a public device or watchdog interface. */
const struct device *system_zephyr_watchdog_device(void);
bool system_zephyr_watchdog_running(void);
#endif
