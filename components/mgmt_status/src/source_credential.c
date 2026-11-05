/* A thin provider of what the credential store declares, in the words a client can show. The getter
 * reads a copy under a short lock of its own, so this never waits on storage. */
#include "mgmt_status_port.h"

#include <crux/credential_store.h>

static const char *state_word(crux_credential_state_t state)
{
    switch (state) {
    case CRUX_CREDENTIAL_STATE_UNUSABLE:
        return "unusable";
    case CRUX_CREDENTIAL_STATE_EMPTY:
        return "empty";
    case CRUX_CREDENTIAL_STATE_VALID:
        return "valid";
    }
    return "unusable";
}

static const char *basis_word(crux_credential_basis_t basis)
{
    switch (basis) {
    case CRUX_CREDENTIAL_BASIS_NONE:
        return "none";
    case CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY:
        return "device_id_key";
    case CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND:
        return "hardware_bound";
    case CRUX_CREDENTIAL_BASIS_UNDECLARED:
        return "undeclared";
    }
    return "undeclared";
}

static const char *evidence_word(crux_credential_evidence_t evidence)
{
    switch (evidence) {
    case CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE:
        return "host_fake";
    case CRUX_CREDENTIAL_EVIDENCE_SOFTWARE:
        return "software";
    case CRUX_CREDENTIAL_EVIDENCE_PHYSICAL:
        return "physical";
    case CRUX_CREDENTIAL_EVIDENCE_UNDECLARED:
        return "undeclared";
    }
    return "undeclared";
}

bool crux_mgmt_status_port_has_credential(void)
{
    return true;
}

crux_status_t crux_mgmt_status_port_credential(crux_mgmt_status_credential_t *out)
{
    crux_credential_store_info_t info;
    crux_status_t result = crux_credential_store_get_info(&info);
    if (result == CRUX_OK) {
        out->state = state_word(info.state);
        out->basis = basis_word(info.basis);
        out->evidence = evidence_word(info.evidence);
        out->confidential = info.confidential_against_medium_reader;
        out->forgery_detected = info.forgery_detected_without_device_id;
        out->rollback_detected = info.rollback_detected;
    }
    return result;
}
