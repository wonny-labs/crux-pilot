#ifndef CRUX_LOGGING_BINDING_H
#define CRUX_LOGGING_BINDING_H
#if defined(CONFIG_LOG_BACKEND_UART)
#include <zephyr/device.h>
const struct device *crux_log_zephyr_device(void);
#endif
#endif
