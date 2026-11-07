/* A thin provider of the event counts. The getter takes no lock and needs no initialisation. */
#include "mgmt_status_port.h"

#include <crux/security_event.h>

bool crux_mgmt_status_port_has_security_events(void)
{
    return true;
}

crux_status_t crux_mgmt_status_port_security_events(crux_mgmt_status_event_counts_t *out)
{
    crux_security_event_stats_t stats;
    crux_status_t result = crux_security_event_get_stats(&stats);
    if (result == CRUX_OK) {
        out->emitted = stats.emitted;
        out->dropped = stats.dropped;
        out->delivered = stats.delivered;
        out->log_errors = stats.log_errors;
    }
    return result;
}
