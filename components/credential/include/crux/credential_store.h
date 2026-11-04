/**
 * @file
 * @brief The credential store's state, and what protects it.
 *
 * Holds what a status page or a project needs to display the store, so that a state never appears
 * without its basis. Reading secrets needs the adapter header, which projects do not get.
 *
 * Thread context. crux_credential_store_get_info() reads a copy published under a short lock of its
 * own, so it never waits on storage. See docs/credential-store.md.
 */
#ifndef CRUX_CREDENTIAL_STORE_H
#define CRUX_CREDENTIAL_STORE_H

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
