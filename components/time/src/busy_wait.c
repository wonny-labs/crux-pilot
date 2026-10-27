#include "busy_wait_port.h"
#include <crux/time.h>

crux_status_t crux_busy_wait(crux_duration_t duration)
{
    const int64_t duration_ns = crux_duration_to_ns(duration);
    if (duration_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (duration_ns == 0) {
        return CRUX_OK;
    }
    return crux_busy_wait_impl_ns(duration_ns);
}
