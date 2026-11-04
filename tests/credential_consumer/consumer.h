#ifndef CRUX_CREDENTIAL_CONSUMER_H
#define CRUX_CREDENTIAL_CONSUMER_H

#include <crux/credential_store.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What a status page needs: the state, and never without its basis. */
crux_status_t credential_consumer_show(crux_credential_state_t *state,
                                       crux_credential_basis_t *basis);

#ifdef __cplusplus
}
#endif

#endif
