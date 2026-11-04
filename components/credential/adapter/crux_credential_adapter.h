/**
 * @file
 * @brief Records and device keys for the Access store port, TLS and SSH providers and a
 * composition. Projects must not include this header.
 *
 * These operations take no session. They are the trust boundary below the services: a service
 * checks its permission, then calls them. No management route, command or file area may expose
 * crux_credential_read() of a private key.
 *
 * Thread context only. Every operation here but crux_credential_state(), crux_credential_roots()
 * and crux_credential_key_parse() can wait on storage, serialises on the store lock and holds at
 * most one File handle, so none of them runs under a permission check. Buffers are borrowed for the
 * call. A caller that received a secret clears its copy.
 *
 * Malformed arguments are refused first, then a store that has not started, then an unusable one.
 *
 * On the pilot basis the store is not confidential against someone who can read the medium and the
 * chip ID. See docs/credential-store.md.
 */
#ifndef CRUX_CREDENTIAL_ADAPTER_H
#define CRUX_CREDENTIAL_ADAPTER_H

#include <crux/credential_store.h>
#include <crux/file.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the sealed image holds. Each is absent until first written. */
typedef enum {
    CRUX_CREDENTIAL_RECORD_ACCOUNTS = 1, /**< Bytes owned by crux_access. */
    CRUX_CREDENTIAL_RECORD_TLS_KEY = 2,  /**< A key record, see crux_credential_key_parse(). */
    CRUX_CREDENTIAL_RECORD_TLS_CERTIFICATE = 3, /**< Bytes owned by the TLS provider. */
    CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY = 4 /**< A key record, see crux_credential_key_parse(). */
} crux_credential_record_t;

/** Store results; zero is success and every other value is a refusal or a failure. */
typedef enum {
    CRUX_CREDENTIAL_OK = 0,
    CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT = -1,
    CRUX_CREDENTIAL_ERR_NOT_READY = -2, /**< Not started, or the store lock could not be taken. */
    CRUX_CREDENTIAL_ERR_UNUSABLE =
        -3, /**< State is UNUSABLE. Nothing is read, written or generated. */
    CRUX_CREDENTIAL_ERR_NOT_FOUND = -4, /**< Record absent from an EMPTY or VALID store. */
    CRUX_CREDENTIAL_ERR_TOO_LARGE =
        -5, /**< Buffer too small, a record over its limit, or a full slot. */
    CRUX_CREDENTIAL_ERR_ENTROPY = -6, /**< No random data for a key or nonce. Nothing was stored. */
    /**
     * A save failed. The previous image stays active, except that a failure writing the second slot
     * for an added record leaves that record active, and it survives a reset (decision S7). Stored
     * bytes can change either way, and a failed save can win at the next boot if the store could
     * not then invalidate it.
     */
    CRUX_CREDENTIAL_ERR_STORE = -7,
    CRUX_CREDENTIAL_ERR_READ = -8 /**< A runtime read failed. Store state is unchanged. */
} crux_credential_result_t;

/** Device key types. SSH user keys are not stored here: they belong to the account record. */
typedef enum {
    CRUX_CREDENTIAL_KEY_ECDSA_P256 = 1, /**< 65-byte uncompressed point, 32-byte private scalar. */
    CRUX_CREDENTIAL_KEY_ED25519 = 2     /**< 32-byte public key, 32-byte private seed. */
} crux_credential_key_type_t;

/** A key record's parts, pointing into the record passed to crux_credential_key_parse(). */
typedef struct {
    crux_credential_key_type_t type;
    const uint8_t *public_key;
    size_t public_length;
    const uint8_t *private_key;
    size_t private_length;
} crux_credential_key_view_t;

/** Largest record of each kind. A read into a buffer of this size cannot return TOO_LARGE. */
#define CRUX_CREDENTIAL_ACCOUNTS_BYTES_MAX 8192u
#define CRUX_CREDENTIAL_CERTIFICATE_BYTES_MAX 4096u
#define CRUX_CREDENTIAL_KEY_RECORD_BYTES_MAX 101u
/** Largest public key from crux_credential_key_public(). */
#define CRUX_CREDENTIAL_KEY_PUBLIC_BYTES_MAX 65u

/**
 * Classify the two slots and make the store usable. A composition calls this once on every boot
 * path, whether or not the medium mounted, before any other thread uses the store and before
 * crux_access_init(). An unbound root gives UNUSABLE, which lasts until the next reset.
 *
 * @param slot_a File root of the first slot. No file area may name it.
 * @param slot_b File root of the second slot, on a path that does not overlap the first.
 * @retval CRUX_CREDENTIAL_OK The state is EMPTY or VALID.
 * @retval CRUX_CREDENTIAL_ERR_UNUSABLE Started, and the state is UNUSABLE.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT Equal roots, or a repeated call. Nothing changed.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY No lock was available. Not started.
 */
crux_credential_result_t crux_credential_start(crux_file_root_t slot_a, crux_file_root_t slot_b);

/**
 * Copy the roots passed to crux_credential_start(), for the file service's reserved list.
 * @param[out] roots Slot A, then slot B. Unchanged on failure.
 * @retval CRUX_CREDENTIAL_OK Copied, whichever state the store is in.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT The output is missing.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY Not started.
 */
crux_credential_result_t crux_credential_roots(crux_file_root_t roots[2]);

/**
 * Report the store's state. EMPTY becomes VALID at the first successful save, and erase makes it
 * UNUSABLE. Never waits on storage.
 * @return UNUSABLE until crux_credential_start() has run, and the current state after it.
 */
crux_credential_state_t crux_credential_state(void);

/**
 * Copy a record. The caller clears a copy that holds a secret.
 *
 * @param[out] buffer May be NULL when capacity is zero. Written only on CRUX_CREDENTIAL_OK.
 * @param[out] length The record's size for OK and TOO_LARGE, and zero otherwise.
 * @retval CRUX_CREDENTIAL_OK Copied.
 * @retval CRUX_CREDENTIAL_ERR_NOT_FOUND The record is absent.
 * @retval CRUX_CREDENTIAL_ERR_TOO_LARGE The buffer is too small.
 * @retval CRUX_CREDENTIAL_ERR_READ The medium could not be read. The state is unchanged.
 * @retval CRUX_CREDENTIAL_ERR_UNUSABLE The state is UNUSABLE.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY Not started.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT An unknown record, or a missing output.
 */
crux_credential_result_t crux_credential_read(crux_credential_record_t record, void *buffer,
                                              size_t capacity, size_t *length);

/**
 * Save a record. Completes, or leaves the previous image active across power loss, subject to the
 * limits of the medium. A record the image did not have reaches both slots before this returns, so
 * a later fault in one slot cannot make an acknowledged record absent.
 *
 * @param data A key record must parse with crux_credential_key_parse().
 * @param length Above zero and at most the record's limit.
 * @retval CRUX_CREDENTIAL_OK Saved.
 * @retval CRUX_CREDENTIAL_ERR_TOO_LARGE Over the record's limit, or the image would not fit its
 * slot.
 * @retval CRUX_CREDENTIAL_ERR_ENTROPY No nonce. Nothing was stored.
 * @retval CRUX_CREDENTIAL_ERR_STORE The save failed. See crux_credential_result_t.
 * @retval CRUX_CREDENTIAL_ERR_UNUSABLE The state is UNUSABLE.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY Not started.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT An unknown record, no data, or a malformed key
 * record.
 */
crux_credential_result_t crux_credential_write(crux_credential_record_t record, const void *data,
                                               size_t length);

/**
 * Generate a device key if its record is absent from a readable store, and save it in both slots
 * before returning, so that a key is never used before it can survive a reset and damage to either
 * slot. An existing key that only one slot holds, because an earlier call failed, is written to the
 * other. Its value and type are left alone: use crux_credential_key_parse() to learn the type.
 *
 * Call this at each boot before using a key, and again after an error.
 *
 * @param record CRUX_CREDENTIAL_RECORD_TLS_KEY or CRUX_CREDENTIAL_RECORD_SSH_HOST_KEY.
 * @retval CRUX_CREDENTIAL_OK Both slots hold the key. Do not use the key otherwise.
 * @retval CRUX_CREDENTIAL_ERR_ENTROPY No random data, or the key could not be generated.
 * @retval CRUX_CREDENTIAL_ERR_READ The medium could not be read, so absence is unproven. No key was
 * generated.
 * @retval CRUX_CREDENTIAL_ERR_TOO_LARGE The image with a new key would not fit its slot. Nothing
 * was stored.
 * @retval CRUX_CREDENTIAL_ERR_STORE The save failed, or the other slot could not be completed. See
 * crux_credential_result_t.
 * @retval CRUX_CREDENTIAL_ERR_UNUSABLE The state is UNUSABLE. No key was generated.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY Not started.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT Not a key record, or an unknown type.
 */
crux_credential_result_t crux_credential_key_ensure(crux_credential_record_t record,
                                                    crux_credential_key_type_t type);

/**
 * Replace a device key with a new one, the deliberate rotation path. Clients will see the change.
 * The older slot keeps the old key until the next save.
 *
 * @retval CRUX_CREDENTIAL_OK Saved. The other slot still holds the old key until the next save.
 * @retval CRUX_CREDENTIAL_ERR_NOT_FOUND There is no key to rotate.
 * @retval CRUX_CREDENTIAL_ERR_TOO_LARGE The image with the new key would not fit its slot. Nothing
 * was stored.
 * @return Otherwise as crux_credential_key_ensure().
 */
crux_credential_result_t crux_credential_key_replace(crux_credential_record_t record,
                                                     crux_credential_key_type_t type);

/**
 * Copy a device key's public part, for a fingerprint or a certificate request.
 *
 * @param[out] buffer May be NULL when capacity is zero. Written only on CRUX_CREDENTIAL_OK.
 * @param[out] length The public part's size for OK and TOO_LARGE, and zero otherwise.
 * @retval CRUX_CREDENTIAL_OK Copied.
 * @retval CRUX_CREDENTIAL_ERR_NOT_FOUND The record is absent.
 * @retval CRUX_CREDENTIAL_ERR_TOO_LARGE The buffer is too small.
 * @retval CRUX_CREDENTIAL_ERR_READ The medium could not be read. The state is unchanged.
 * @retval CRUX_CREDENTIAL_ERR_UNUSABLE The state is UNUSABLE.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY Not started.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT Not a key record, or a missing output.
 */
crux_credential_result_t crux_credential_key_public(crux_credential_record_t record, void *buffer,
                                                    size_t capacity, size_t *length);

/**
 * Split a key record read with crux_credential_read(). Touches no storage or lock. The record holds
 * a type byte, the public length, the private length and a zero byte, then the public and private
 * parts, whose sizes are fixed by the type.
 *
 * @param[out] view Points into record, so it holds a secret for as long as the record does.
 * Unchanged on failure.
 * @retval CRUX_CREDENTIAL_OK Split.
 * @retval CRUX_CREDENTIAL_ERR_INVALID_ARGUMENT A missing argument, or a malformed record.
 */
crux_credential_result_t crux_credential_key_parse(const void *record, size_t length,
                                                   crux_credential_key_view_t *view);

/**
 * Overwrite both slots with zeros, sync and remove them, in any state after start. The state is
 * UNUSABLE from then until the next reset, because provisioning needs a store that was EMPTY at
 * start. This does not prove physical erasure: a card's internal mapping can keep old pages.
 *
 * A composition wires this to its own local trigger. The reference offers no command for it,
 * because a serial command without a login would lower limit A7 from "medium and serial" to "serial
 * alone".
 *
 * @retval CRUX_CREDENTIAL_OK Both slots are gone.
 * @retval CRUX_CREDENTIAL_ERR_STORE A step failed. The operator repeats it.
 * @retval CRUX_CREDENTIAL_ERR_NOT_READY Not started. Nothing was done.
 */
crux_credential_result_t crux_credential_erase(void);

#ifdef __cplusplus
}
#endif

#endif
