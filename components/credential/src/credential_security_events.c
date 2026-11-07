/* The event port of the credential store, as a thin translation onto the public emit. It holds no
 * state and takes no lock of its own, and a store event holds no record bytes, so none reaches a
 * record here. The store acts for no session, so no event names a user or a channel. */
#include "credential_port.h"

#include <crux/security_event.h>

static crux_security_event_result_t result_from_store(crux_credential_result_t result)
{
    switch (result) {
    case CRUX_CREDENTIAL_OK:
        return CRUX_SECURITY_EVENT_OK;
    case CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT:
        return CRUX_SECURITY_EVENT_INVALID;
    case CRUX_CREDENTIAL_ERR_TOO_LARGE:
        return CRUX_SECURITY_EVENT_LIMIT;
    case CRUX_CREDENTIAL_ERR_NOT_READY:
    case CRUX_CREDENTIAL_ERR_UNUSABLE:
    case CRUX_CREDENTIAL_ERR_NOT_FOUND:
    case CRUX_CREDENTIAL_ERR_ENTROPY:
    case CRUX_CREDENTIAL_ERR_STORE:
    case CRUX_CREDENTIAL_ERR_READ:
        break;
    }
    return CRUX_SECURITY_EVENT_FAILED;
}

static const char *state_subject(crux_credential_state_t state)
{
    switch (state) {
    case CRUX_CREDENTIAL_STATE_EMPTY:
        return "empty";
    case CRUX_CREDENTIAL_STATE_VALID:
        return "valid";
    case CRUX_CREDENTIAL_STATE_UNUSABLE:
        break;
    }
    return "unusable";
}

static const char *record_subject(crux_credential_record_t record)
{
    switch (record) {
    case CRUX_CREDENTIAL_RECORD_ACCOUNTS:
        return "accounts";
    case CRUX_CREDENTIAL_RECORD_TLS_KEY:
        return "tls_key";
    case CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE:
        return "tls_certificate";
    case CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY:
        return "ssh_host_key";
    }
    return NULL;
}

void crux_credential_port_event(const crux_credential_event_t *event)
{
    crux_security_event_t record = {.result = result_from_store(event->result)};

    /* No default case: the compiler refuses a port event that this table does not map. */
    switch (event->type) {
    case CRUX_CREDENTIAL_EVENT_STATE:
        record.id = CRUX_SECURITY_EVENT_ID_CREDENTIAL_STATE;
        /* The proposal's mapping: a state is a result only by being unusable. */
        record.result = event->state == CRUX_CREDENTIAL_STATE_UNUSABLE ? CRUX_SECURITY_EVENT_FAILED
                                                                       : CRUX_SECURITY_EVENT_OK;
        record.subject = state_subject(event->state);
        break;
    case CRUX_CREDENTIAL_EVENT_SLOT_INVALID:
        record.id = CRUX_SECURITY_EVENT_ID_CREDENTIAL_SLOT_INVALID;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        record.subject = event->slot == 'a' ? "a" : "b";
        break;
    case CRUX_CREDENTIAL_EVENT_SAVE_FAILED:
        record.id = CRUX_SECURITY_EVENT_ID_CREDENTIAL_SAVE_FAILED;
        break;
    case CRUX_CREDENTIAL_EVENT_KEY_GENERATED:
        record.id = CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_GENERATED;
        record.result = CRUX_SECURITY_EVENT_OK;
        record.subject = record_subject(event->record);
        break;
    case CRUX_CREDENTIAL_EVENT_KEY_REPLACED:
        record.id = CRUX_SECURITY_EVENT_ID_CREDENTIAL_KEY_REPLACED;
        record.result = CRUX_SECURITY_EVENT_OK;
        record.subject = record_subject(event->record);
        break;
    case CRUX_CREDENTIAL_EVENT_ERASED:
        record.id = CRUX_SECURITY_EVENT_ID_CREDENTIAL_ERASED;
        break;
    }
    crux_security_event_emit(&record);
}
