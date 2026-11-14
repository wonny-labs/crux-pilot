#ifndef CRUX_SYSTEM_SIM_WATCHDOG_H
#define CRUX_SYSTEM_SIM_WATCHDOG_H
#include <stdbool.h>
#include <zephyr/device.h>
/* Private to the System Zephyr provider, built only under
 * CONFIG_CRUX_SYSTEM_NATIVE_SIM_WATCHDOG on native_sim. Not a public interface. */
const struct device *crux_system_sim_watchdog_device(void);
bool crux_system_sim_watchdog_running(void);
#endif
