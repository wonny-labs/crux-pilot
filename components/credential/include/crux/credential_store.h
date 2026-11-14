/**
 * @file
 * @brief The credential store's state, and what protects it.
 *
 * Holds what a status page or a project needs to display the store, so that a state never appears
 * without its basis. Reading secrets needs the adapter header, which projects do not get.
 *
 * Status: Experimental (ADR-0001 decision 4). Access and the security events that it is wired to
 * have no Zephyr composition outside tests/ yet, and a change is listed in the release notes.
 *
 * Thread context. crux_credential_store_get_info() reads a copy published under a short lock of its
 * own, so it never waits on storage. See docs/credential-store.md.
 *
 * A product starts the store with crux_credential_start() and reads its state here. The operations
 * on records and device keys are not part of this interface: the services that need them reach them
 * through a private adapter, and a product never reads or writes a record itself.
 */
#ifndef CRUX_CREDENTIAL_STORE_H
#define CRUX_CREDENTIAL_STORE_H

#include <crux/file.h>
#include <crux/status.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One state for the whole store. Uninitialised storage is UNUSABLE, so that it fails closed. */
typedef enum {
    /** Refuses everything but erase, until the next reset. */
    CRUX_CREDENTIAL_STATE_UNUSABLE = 0,
    /** No readable image exists: first boot or a blanked store. */
    CRUX_CREDENTIAL_STATE_EMPTY = 1,
    /** A sealed image opened. Single records can still be absent. */
    CRUX_CREDENTIAL_STATE_VALID = 2
} crux_credential_state_t;

/** What protects the stored secrets. */
typedef enum {
    /** Test fake only. A product build refuses it. */
    CRUX_CREDENTIAL_BASIS_NONE = 0,
    /** Not confidential against the medium and chip ID. */
    CRUX_CREDENTIAL_BASIS_DEVICE_ID_KEY = 1,
    /** Declared only by a provider with its own evidence. */
    CRUX_CREDENTIAL_BASIS_HARDWARE_BOUND = 2,
    /** The provider cannot run, or its declaration was refused. The state is UNUSABLE. */
    CRUX_CREDENTIAL_BASIS_UNDECLARED = 3
} crux_credential_basis_t;

/** How far the provider's declaration has been shown to hold. */
typedef enum {
    /** A product build refuses it. */
    CRUX_CREDENTIAL_EVIDENCE_HOST_FAKE = 0,
    CRUX_CREDENTIAL_EVIDENCE_SOFTWARE = 1,
    /** The composition states this after approved target runs. */
    CRUX_CREDENTIAL_EVIDENCE_PHYSICAL = 2,
    /** The provider cannot run, or its declaration was refused. The state is UNUSABLE. */
    CRUX_CREDENTIAL_EVIDENCE_UNDECLARED = 3
} crux_credential_evidence_t;

/** Store state and the provider's declaration, always together. */
typedef struct {
    crux_credential_state_t state;
    crux_credential_basis_t basis;
    crux_credential_evidence_t evidence;
    bool confidential_against_medium_reader; /**< False for NONE and DEVICE_ID_KEY. */
    bool forgery_detected_without_device_id;
    bool rollback_detected; /**< False for every provider that exists. */
} crux_credential_store_info_t;

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

/**
 * Classify the two slots and make the store usable. A composition calls this once on every boot
 * path, whether or not the medium mounted, before any other thread uses the store and before
 * crux_access_init(). An unbound root gives UNUSABLE, which lasts until the next reset.
 *
 * Thread context only. It can wait on storage and on the store lock. A product does not generate
 * device keys: crux_tls_certificate_start() ensures the TLS key, and the SSH composition code of
 * phase 3 ensures the SSH host key before it starts the provider.
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
 * Copy the store's state and the provider's declaration.
 *
 * @param[out] out Unchanged on failure.
 * @retval CRUX_OK Copied, whichever state the store is in.
 * @retval CRUX_ERR_INVALID_ARGUMENT The output is missing.
 * @retval CRUX_ERR_NOT_READY The store has not started.
 */
crux_status_t crux_credential_store_get_info(crux_credential_store_info_t *out);

#ifdef __cplusplus
}
#endif

#endif
