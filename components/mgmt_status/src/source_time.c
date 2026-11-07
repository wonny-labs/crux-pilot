/* A thin provider of the Time sources. */
#include "mgmt_status_port.h"

#include <crux/time.h>

crux_status_t crux_mgmt_status_port_uptime_ms(uint64_t *out)
{
    crux_monotonic_time_t now;
    crux_status_t result = crux_monotonic_now(&now);
    if (result == CRUX_OK) {
        *out = (uint64_t)now.private_ns / UINT64_C(1000000);
    }
    return result;
}

crux_status_t crux_mgmt_status_port_clock(crux_mgmt_status_clock_t *out)
{
    crux_realtime_snapshot_t snapshot;
    crux_status_t result = crux_realtime_get_snapshot(&snapshot);
    if (result != CRUX_OK) {
        return result;
    }
    if (!snapshot.is_valid) {
        *out = CRUX_MGMT_STATUS_CLOCK_UNSET;
        return CRUX_OK;
    }
    switch (snapshot.last_source) {
    case CRUX_REALTIME_SOURCE_RTC:
        *out = CRUX_MGMT_STATUS_CLOCK_RTC;
        break;
    case CRUX_REALTIME_SOURCE_MANUAL:
        *out = CRUX_MGMT_STATUS_CLOCK_MANUAL;
        break;
    case CRUX_REALTIME_SOURCE_EXTERNAL:
        *out = CRUX_MGMT_STATUS_CLOCK_EXTERNAL;
        break;
    default:
        *out = CRUX_MGMT_STATUS_CLOCK_UNKNOWN;
        break;
    }
    return CRUX_OK;
}
