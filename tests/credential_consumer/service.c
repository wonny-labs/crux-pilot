#include <crux_credential_adapter.h>

crux_credential_state_t credential_service_state(void);

crux_credential_state_t credential_service_state(void)
{
    return crux_credential_state();
}
