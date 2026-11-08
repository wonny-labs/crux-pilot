/* What a composition selects while it links no credential store. Its items are then omitted. */
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
