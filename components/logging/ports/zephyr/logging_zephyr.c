#include "logging_binding.h"
#include "logging_port.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(crux_logging, LOG_LEVEL_DBG);

bool logging_port_context_allowed(void)
{
    return !k_is_in_isr() && !k_is_pre_kernel();
}

crux_status_t logging_port_write(crux_log_level_t level, const char *tag, const char *text)
{
    if (!device_is_ready(logging_zephyr_device()))
        return CRUX_ERR_NOT_READY;
    /* Native deferred packaging copies these writable strings before return.
     * Its void submission API provides no per-record capacity acknowledgement. */
    switch (level) {
    case CRUX_LOG_ERROR:
        LOG_ERR("%s: %s", tag, text);
        break;
    case CRUX_LOG_WARN:
        LOG_WRN("%s: %s", tag, text);
        break;
    case CRUX_LOG_INFO:
        LOG_INF("%s: %s", tag, text);
        break;
    case CRUX_LOG_DEBUG:
        LOG_DBG("%s: %s", tag, text);
        break;
    default:
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    return CRUX_OK;
}
