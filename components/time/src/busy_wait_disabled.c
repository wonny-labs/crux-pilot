#include "busy_wait_port.h"

crux_status_t crux_busy_wait_ns(int64_t duration_ns)
{
    if (duration_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    return duration_ns == 0 ? CRUX_OK : CRUX_ERR_NOT_SUPPORTED;
}
