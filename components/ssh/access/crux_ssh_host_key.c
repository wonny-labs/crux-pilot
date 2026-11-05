/* The host key's way from the credential store to the provider. See crux_ssh_access.h. */
#include "crux_ssh_access.h"

#include <crux_credential_adapter.h>

#include <string.h>

static void wipe(void *buffer, size_t length)
{
    volatile uint8_t *bytes = buffer;

    for (size_t i = 0; i < length; ++i) {
        bytes[i] = 0;
    }
}

void crux_ssh_host_key_clear(crux_ssh_host_key_t *host_key)
{
    if (host_key != NULL) {
        wipe(host_key, sizeof(*host_key));
    }
}

static bool fill_host_key(const crux_credential_key_view_t *view, crux_ssh_host_key_t *host_key)
{
    size_t length = view->private_length + view->public_length;

    if (length > sizeof(host_key->key)) {
        return false;
    }
    if (view->type == CRUX_CREDENTIAL_KEY_ED25519) {
        host_key->type = CRUX_SSH_KEY_ED25519;
    } else if (view->type == CRUX_CREDENTIAL_KEY_ECDSA_P256) {
        host_key->type = CRUX_SSH_KEY_ECDSA_P256;
    } else {
        return false;
    }
    memcpy(host_key->key, view->private_key, view->private_length);
    memcpy(host_key->key + view->private_length, view->public_key, view->public_length);
    host_key->length = length;
    return true;
}

crux_status_t crux_ssh_host_key_load(crux_ssh_key_type_t type, crux_ssh_host_key_t *host_key)
{
    uint8_t record[CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX];
    crux_credential_key_view_t view;
    crux_credential_key_type_t wanted;
    size_t length = 0;
    crux_status_t status = CRUX_ERR_NOT_READY;

    if (host_key == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_ssh_host_key_clear(host_key);
    if (type == CRUX_SSH_KEY_ED25519) {
        wanted = CRUX_CREDENTIAL_KEY_ED25519;
    } else if (type == CRUX_SSH_KEY_ECDSA_P256) {
        wanted = CRUX_CREDENTIAL_KEY_ECDSA_P256;
    } else {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (crux_credential_key_ensure(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, wanted) ==
            CRUX_CREDENTIAL_OK &&
        crux_credential_read(CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY, record, sizeof(record),
                             &length) == CRUX_CREDENTIAL_OK &&
        crux_credential_key_parse(record, length, &view) == CRUX_CREDENTIAL_OK &&
        fill_host_key(&view, host_key)) {
        status = CRUX_OK;
    }
    wipe(record, sizeof(record));
    return status;
}
