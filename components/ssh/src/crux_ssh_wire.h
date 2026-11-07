/*
 * The checks a provider applies to what a client sends before Crux sees it, and the host key's
 * public form. Crux code only: a project never includes this header, and it is not a stable
 * interface. See docs/ssh-wolf-provider.md#wire-checks.
 *
 * It names no SSH library type, so the Host build tests every check with no SSH library. A provider
 * passes the bytes it parsed from a packet, and each function reads no more than the length given.
 */
#ifndef CRUX_SSH_WIRE_H
#define CRUX_SSH_WIRE_H

#include "crux_ssh_provider.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The private part of a host key, for either type: an Ed25519 seed or a P-256 scalar. */
#define CRUX_SSH_WIRE_HOST_KEY_PRIVATE_BYTES 32u
/** An Ed25519 host key in the form crux_ssh_config_t takes: the seed, then the public key. */
#define CRUX_SSH_WIRE_HOST_KEY_ED25519_BYTES 64u
/** A P-256 host key in that form: the scalar, then the uncompressed point. */
#define CRUX_SSH_WIRE_HOST_KEY_P256_BYTES 97u
/** An SSH public key blob of a type Crux accepts. The P-256 blob is the larger, at 104 bytes. */
#define CRUX_SSH_WIRE_KEY_BLOB_MAX 104u
/** "SHA256:", 43 characters of Base64 with no padding, and a terminator. */
#define CRUX_SSH_WIRE_FINGERPRINT_SIZE 51u
/** The SHA-256 digest that a fingerprint shows. */
#define CRUX_SSH_WIRE_DIGEST_BYTES 32u

/**
 * Checks a user name from an authentication request and terminates a copy of it.
 *
 * @param name The name as sent, not terminated.
 * @param[out] user Holds the name on success, and an empty string otherwise.
 * @return False for NULL, an empty name, one longer than CRUX_SSH_USER_NAME_MAX, or an embedded
 * NUL.
 */
bool crux_ssh_wire_user_name(const uint8_t *name, size_t length,
                             char user[CRUX_SSH_USER_NAME_MAX + 1u]);

/**
 * Checks a public key from an authentication request: its algorithm name and its key blob.
 *
 * The name must be `ssh-ed25519` or `ecdsa-sha2-nistp256`. The blob must be exactly that type's
 * form: the same name, the curve name `nistp256` for ECDSA, and a 32-byte key or a 65-byte
 * uncompressed point, with nothing after it. There is no RSA, DSA, other curve, security key or
 * certificate, by owner decision. It checks form only: the library checks the point when it
 * verifies the signature.
 *
 * @param[out] type The key type on success, and left alone otherwise.
 * @return False for anything else, including NULL.
 */
bool crux_ssh_wire_user_key(const uint8_t *algorithm, size_t algorithm_length, const uint8_t *blob,
                            size_t blob_length, crux_ssh_key_type_t *type);

/**
 * Checks the data of an `exec` request: one SSH string that fills it, of at most command_max bytes,
 * with no NUL.
 *
 * @param[out] command Points into data at the command on success.
 * @param[out] command_length Its length on success.
 */
bool crux_ssh_wire_exec_command(const uint8_t *data, size_t data_length, size_t command_max,
                                const uint8_t **command, size_t *command_length);

/** Whether the data of a `subsystem` request is one SSH string, exactly `sftp`, that fills it. */
bool crux_ssh_wire_subsystem_is_sftp(const uint8_t *data, size_t data_length);

/** Whether a host key of this type has the length and public form of crux_ssh_config_t. */
bool crux_ssh_wire_host_key_form(crux_ssh_key_type_t type, const uint8_t *host_key,
                                 size_t host_key_length);

/**
 * Writes the SSH public key blob of a host key, from its public part alone, as a client sees it.
 *
 * @param[out] blob_length The blob's length on success, and 0 otherwise.
 * @return False if the key fails crux_ssh_wire_host_key_form, a pointer is NULL, or the capacity is
 * less than the blob.
 */
bool crux_ssh_wire_host_key_blob(crux_ssh_key_type_t type, const uint8_t *host_key,
                                 size_t host_key_length, uint8_t *blob, size_t capacity,
                                 size_t *blob_length);

/**
 * Writes a SHA-256 digest as OpenSSH prints a fingerprint: "SHA256:" and Base64 with no padding.
 *
 * @return False for NULL or a buffer smaller than CRUX_SSH_WIRE_FINGERPRINT_SIZE, and then a text
 * buffer of any size above zero holds an empty string.
 */
bool crux_ssh_wire_fingerprint_text(const uint8_t digest[CRUX_SSH_WIRE_DIGEST_BYTES], char *text,
                                    size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_SSH_WIRE_H */
