/*
 * The wolfSSH provider of the private SSH interface. Crux code only: a project never includes this
 * header. It names no SSH library type. See docs/ssh-wolf-provider.md.
 */
#ifndef CRUX_SSH_WOLF_PROVIDER_H
#define CRUX_SSH_WOLF_PROVIDER_H

#include "crux_ssh_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Connections this build can serve at once, each with its own worker thread and stack. */
#ifndef CRUX_SSH_WOLF_CONNECTIONS_MAX
#define CRUX_SSH_WOLF_CONNECTIONS_MAX 4
#endif

/**
 * The host key form of crux_ssh_config_t on this provider: the private part, then the public part.
 * That is the 32-byte seed and the 32-byte public key for Ed25519, 64 bytes in all, and the 32-byte
 * scalar and the 65-byte uncompressed point for ECDSA P-256, 97 bytes in all. These are the parts
 * of a credential store key record, and any other length is CRUX_ERR_IO from start().
 */
const crux_ssh_provider_t *crux_ssh_wolf_provider(void);

/** Buffer for crux_ssh_wolf_host_key_fingerprint(), with its terminator. */
#define CRUX_SSH_WOLF_FINGERPRINT_SIZE 51u

/**
 * Writes the key's fingerprint as OpenSSH prints it, "SHA256:" and 43 characters of Base64 with no
 * padding, for display on the serial console and to a principal with access.manage. It reads only
 * the public part.
 *
 * @retval CRUX_OK Written, and terminated.
 * @retval CRUX_ERR_INVALID_ARGUMENT A pointer is NULL, the buffer is too small, or the key is of an
 * unknown type, the wrong length, or for P-256 its public part is not an uncompressed point.
 * @retval CRUX_ERR_IO The hash failed.
 */
crux_status_t crux_ssh_wolf_host_key_fingerprint(crux_ssh_key_type_t type, const uint8_t *host_key,
                                                 size_t host_key_length, char *fingerprint,
                                                 size_t size);

#ifdef CRUX_SSH_WOLF_TESTING
/* Test images only. What a test client cannot do from the far end of a real socket, and the
 * measurements that only this file can take. A connection is named by the client's port. */
void crux_ssh_wolf_test_drop_on_write(uint16_t peer_port);
crux_ssh_file_handle_t *crux_ssh_wolf_test_file_handle(uint16_t peer_port);
/** Every nth send reports that the socket would block. Zero stops this. */
void crux_ssh_wolf_test_block_sends(unsigned period);
/** On native_sim, the deepest host stack seen on a worker since boot, and on a finished listener.
 */
void crux_ssh_wolf_test_stack_used(size_t *worker, size_t *listener);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_WOLF_PROVIDER_H */
