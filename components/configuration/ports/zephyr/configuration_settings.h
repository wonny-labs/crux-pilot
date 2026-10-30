#ifndef CRUX_CONFIGURATION_SETTINGS_H
#define CRUX_CONFIGURATION_SETTINGS_H

#include <crux/configuration.h>

/* Private simulator composition: caller-serialized task context, explicit init. */
crux_config_result_t crux_config_zephyr_init(void);
#endif
