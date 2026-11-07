/*
 * What a composition needs from the access side of SSH, beyond the entry points in
 * crux_ssh_callbacks.h. Crux code only: a project never includes this header.
 */
#ifndef CRUX_SSH_ACCESS_H
#define CRUX_SSH_ACCESS_H

#include "crux_ssh_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Authenticated connections at once. No smaller than the provider's max_connections. */
#ifndef CRUX_SSH_ACCESS_ATTACHMENT_MAX
#define CRUX_SSH_ACCESS_ATTACHMENT_MAX 8u
#endif

/** Largest host key in the provider's form: a P-256 scalar and its uncompressed point. */
#define CRUX_SSH_HOST_KEY_BYTES_MAX 97u

/**
 * A host key in the form crux_ssh_config_t takes: the private part, then the public part. That is
 * 32 + 32 bytes for Ed25519 and 32 + 65 bytes for ECDSA P-256. It holds a secret.
 */
typedef struct {
    crux_ssh_key_type_t type;
    uint8_t key[CRUX_SSH_HOST_KEY_BYTES_MAX];
    size_t length;
} crux_ssh_host_key_t;

/**
 * Gets the device's host key from the credential store, generating it on first use.
 *
 * A key that already exists keeps its type, whatever is asked for. So the type in the result, and
 * not the argument, is what the server offers. The store must have started.
 *
 * @param type The type to generate if there is no key.
 * @param[out] host_key Holds the key on CRUX_OK, and is cleared otherwise.
 * @retval CRUX_OK Loaded.
 * @retval CRUX_ERR_INVALID_ARGUMENT The output is NULL, or the type is unknown.
 * @retval CRUX_ERR_NOT_READY The store could not supply a key. The server must not start, and no
 * other key is generated in its place.
 */
crux_status_t crux_ssh_host_key_load(crux_ssh_key_type_t type, crux_ssh_host_key_t *host_key);

/** Wipes a key once the provider has stopped. NULL does nothing. */
void crux_ssh_host_key_clear(crux_ssh_host_key_t *host_key);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_ACCESS_H */
