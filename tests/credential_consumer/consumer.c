#include "consumer.h"

crux_status_t credential_consumer_show(crux_credential_state_t *state,
                                       crux_credential_basis_t *basis)
{
    crux_credential_store_info_t info;
    crux_status_t status = crux_credential_store_get_info(&info);
    if (status == CRUX_OK) {
        *state = info.state;
        *basis = info.basis;
    }
    return status;
}
