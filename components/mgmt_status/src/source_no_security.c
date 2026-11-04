/* What a composition selects while it links no credential store (#178) and no security events
 * (#182). Their items are then omitted. When one of them lands, a thin provider replaces its half.
 */
#include "mgmt_status_port.h"

bool crux_mgmt_status_port_has_credential(void)
{
    return false;
}

crux_status_t crux_mgmt_status_port_credential(crux_mgmt_status_credential_t *out)
{
    (void)out;
    return CRUX_ERR_NOT_SUPPORTED;
}

bool crux_mgmt_status_port_has_security_events(void)
{
    return false;
}

crux_status_t crux_mgmt_status_port_security_events(crux_mgmt_status_event_counts_t *out)
{
    (void)out;
    return CRUX_ERR_NOT_SUPPORTED;
}
