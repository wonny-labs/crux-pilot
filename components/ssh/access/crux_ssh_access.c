/*
 * From a connection to crux_access: the mapping that docs/ssh-provider-contract.md records. Every
 * call runs on the worker thread of its connection, so a slow verification holds that connection
 * only. Every access result other than success is a denial on the wire.
 */
#include "crux_ssh_access.h"
#include "crux_ssh_attachment.h"
#include "crux_ssh_callbacks.h"

#include <crux_access_adapter.h>

#include <stdatomic.h>
#include <string.h>

#define IPV4_LENGTH 4u
#define IPV6_LENGTH 16u
#define IPV6_PREFIX_LENGTH 8u
#define IPV4_MAPPED_PREFIX_LENGTH 12u

struct crux_ssh_attachment {
    atomic_bool in_use;
    crux_access_session_t session;
};

static crux_ssh_attachment_t attachments[CRUX_SSH_ACCESS_ATTACHMENT_MAX];

static crux_ssh_attachment_t *attachment_take(void)
{
    for (size_t i = 0; i < CRUX_SSH_ACCESS_ATTACHMENT_MAX; ++i) {
        bool expected = false;

        if (atomic_compare_exchange_strong(&attachments[i].in_use, &expected, true)) {
            return &attachments[i];
        }
    }
    return NULL;
}

static void attachment_release(crux_ssh_attachment_t *attachment)
{
    attachment->session.value = 0u;
    atomic_store(&attachment->in_use, false);
}

static bool is_ipv4_mapped(const crux_ssh_peer_t *peer)
{
    static const uint8_t prefix[IPV4_MAPPED_PREFIX_LENGTH] = {0, 0, 0, 0, 0,    0,
                                                              0, 0, 0, 0, 0xff, 0xff};

    return memcmp(peer->address, prefix, sizeof(prefix)) == 0;
}

/* A source is what one party is likely to control: an IPv4 address, or an IPv6 /64. */
static crux_access_origin_t origin_of(const crux_ssh_peer_t *peer)
{
    crux_access_origin_t origin = {.channel = CRUX_ACCESS_CHANNEL_SSH};

    if (peer->address_length == IPV4_LENGTH) {
        memcpy(origin.source, peer->address, IPV4_LENGTH);
        origin.source_length = IPV4_LENGTH;
    } else if (peer->address_length == IPV6_LENGTH && is_ipv4_mapped(peer)) {
        memcpy(origin.source, &peer->address[IPV4_MAPPED_PREFIX_LENGTH], IPV4_LENGTH);
        origin.source_length = IPV4_LENGTH;
    } else if (peer->address_length == IPV6_LENGTH) {
        memcpy(origin.source, peer->address, IPV6_PREFIX_LENGTH);
        origin.source_length = IPV6_PREFIX_LENGTH;
    }
    return origin;
}

crux_ssh_auth_result_t crux_ssh_access_password(void *context, const crux_ssh_peer_t *peer,
                                                const char *user, const uint8_t *password,
                                                size_t password_length,
                                                crux_ssh_attachment_t **attachment)
{
    const crux_access_origin_t origin = origin_of(peer);
    crux_ssh_attachment_t *taken = attachment_take();
    uint32_t retry_after_ms = 0;

    (void)context;
    if (taken == NULL) {
        return CRUX_SSH_AUTH_DENIED;
    }
    if (crux_access_login_password(user, (const char *)password, password_length, &origin,
                                   &taken->session, &retry_after_ms) != CRUX_ACCESS_OK) {
        attachment_release(taken);
        return CRUX_SSH_AUTH_DENIED;
    }
    *attachment = taken;
    return CRUX_SSH_AUTH_ACCEPTED;
}

crux_ssh_auth_result_t crux_ssh_access_key_check(void *context, const crux_ssh_peer_t *peer,
                                                 const char *user, crux_ssh_key_type_t key_type,
                                                 const uint8_t *key, size_t key_length,
                                                 bool signed_request)
{
    const crux_access_origin_t origin = origin_of(peer);
    uint32_t retry_after_ms = 0;

    (void)context;
    (void)key_type;
    return crux_access_key_is_authorised(user, key, key_length, signed_request, &origin,
                                         &retry_after_ms) == CRUX_ACCESS_OK
               ? CRUX_SSH_AUTH_ACCEPTED
               : CRUX_SSH_AUTH_DENIED;
}

crux_ssh_auth_result_t crux_ssh_access_key_verified(void *context, const crux_ssh_peer_t *peer,
                                                    const char *user, crux_ssh_key_type_t key_type,
                                                    const uint8_t *key, size_t key_length,
                                                    crux_ssh_attachment_t **attachment)
{
    const crux_access_origin_t origin = origin_of(peer);
    crux_ssh_attachment_t *taken = attachment_take();

    (void)context;
    (void)key_type;
    if (taken == NULL) {
        return CRUX_SSH_AUTH_DENIED;
    }
    if (crux_access_login_verified(user, key, key_length, &origin, &taken->session) !=
        CRUX_ACCESS_OK) {
        attachment_release(taken);
        return CRUX_SSH_AUTH_DENIED;
    }
    *attachment = taken;
    return CRUX_SSH_AUTH_ACCEPTED;
}

void crux_ssh_access_key_failed(void *context, const crux_ssh_peer_t *peer, const char *user)
{
    const crux_access_origin_t origin = origin_of(peer);

    (void)context;
    crux_access_report_key_failure(user, &origin);
}

bool crux_ssh_access_session_alive(void *context, crux_ssh_attachment_t *attachment)
{
    char name[CRUX_ACCESS_NAME_SIZE];

    (void)context;
    /* A session query refreshes nothing, so polling cannot keep an idle session alive. */
    return attachment != NULL &&
           crux_access_session_user(attachment->session, name, sizeof(name)) == CRUX_ACCESS_OK;
}

void crux_ssh_access_connection_closed(void *context, crux_ssh_attachment_t *attachment)
{
    (void)context;
    if (attachment != NULL) {
        crux_access_logout(&attachment->session);
        attachment_release(attachment);
    }
}

crux_access_session_t crux_ssh_attachment_session(const crux_ssh_attachment_t *attachment)
{
    crux_access_session_t none = {0};

    return attachment != NULL ? attachment->session : none;
}
