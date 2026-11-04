/*
 * Refuses every login until #192 replaces it with the mapping onto crux_access that
 * docs/ssh-provider-contract.md records. It is here so that the table links from the start.
 */
#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"

crux_ssh_auth_result_t crux_ssh_access_password(void *context, const crux_ssh_peer_t *peer,
                                                const char *user, const uint8_t *password,
                                                size_t password_length,
                                                crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)password;
    (void)password_length;
    (void)attachment;
    return CRUX_SSH_AUTH_DENIED;
}

crux_ssh_auth_result_t crux_ssh_access_key_check(void *context, const crux_ssh_peer_t *peer,
                                                 const char *user, crux_ssh_key_type_t key_type,
                                                 const uint8_t *key, size_t key_length,
                                                 bool signed_request)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)key_type;
    (void)key;
    (void)key_length;
    (void)signed_request;
    return CRUX_SSH_AUTH_DENIED;
}

crux_ssh_auth_result_t crux_ssh_access_key_verified(void *context, const crux_ssh_peer_t *peer,
                                                    const char *user, crux_ssh_key_type_t key_type,
                                                    const uint8_t *key, size_t key_length,
                                                    crux_ssh_attachment_t **attachment)
{
    (void)context;
    (void)peer;
    (void)user;
    (void)key_type;
    (void)key;
    (void)key_length;
    (void)attachment;
    return CRUX_SSH_AUTH_DENIED;
}

void crux_ssh_access_key_failed(void *context, const crux_ssh_peer_t *peer, const char *user)
{
    (void)context;
    (void)peer;
    (void)user;
}

bool crux_ssh_access_session_alive(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    (void)attachment;
    return false;
}

void crux_ssh_access_connection_closed(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    (void)attachment;
}

crux_access_session_t crux_ssh_attachment_session(const crux_ssh_attachment_t *attachment)
{
    crux_access_session_t none = {0};

    (void)attachment;
    return none;
}
