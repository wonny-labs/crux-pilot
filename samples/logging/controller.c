#include "controller.h"
crux_status_t logging_controller_state(bool healthy)
{
    return crux_log_write(healthy ? CRUX_LOG_INFO : CRUX_LOG_ERROR, "controller",
                          healthy ? "healthy" : "fault");
}
