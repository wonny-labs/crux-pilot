/* What a composition selects while it links no security events. Their items are then omitted. */
#include "mgmt_status_port.h"

bool crux_mgmt_status_port_has_security_events(void)
{
    return false;
}

crux_status_t crux_mgmt_status_port_security_events(crux_mgmt_status_event_counts_t *out)
{
    (void)out;
    return CRUX_ERR_NOT_SUPPORTED;
}
