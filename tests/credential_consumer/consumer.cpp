#include "consumer.h"

namespace
{
bool credential_is_usable()
{
    crux_credential_state_t state = CRUX_CREDENTIAL_STATE_UNUSABLE;
    crux_credential_basis_t basis = CRUX_CREDENTIAL_BASIS_NONE;
    return credential_consumer_show(&state, &basis) == CRUX_OK &&
           state != CRUX_CREDENTIAL_STATE_UNUSABLE;
}
} // namespace

bool credential_consumer_usable();

bool credential_consumer_usable()
{
    return credential_is_usable();
}
