/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Source: docs/plans/image-state-contract-proposal.md (milestone M9, issue #297), status
 * "Proposed, not adopted". This is not a public interface: it is not under any component's
 * include/ directory, no CMake target exports it, and no product may include it. The identifiers
 * follow the proposal's illustrative signatures one to one so that a reviewer can compare them.
 *
 * Portable C17. No Zephyr, MCUboot, bootutil, board or vendor header.
 */
#ifndef CRUX_IMAGE_STATE_PROTO_H
#define CRUX_IMAGE_STATE_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRUX_IMAGE_SLOT_COUNT 2u /* one image, two slots: no multi-image */

typedef uint8_t crux_image_slot_t; /* 0 or 1; equal to the bootloader's slot number */

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint16_t revision;
    uint32_t build; /* carried, never compared */
} crux_image_version_t;

typedef enum {
    CRUX_IMAGE_SLOT_EMPTY = 0,             /* no valid header */
    CRUX_IMAGE_SLOT_STAGED = 1,            /* valid header, no trailer: written, not requested */
    CRUX_IMAGE_SLOT_PENDING_TEST = 2,      /* requested; boots on the next reset if selected */
    CRUX_IMAGE_SLOT_PENDING_PERMANENT = 3, /* reported only; this contract never creates it */
    CRUX_IMAGE_SLOT_RUNNING_CONFIRMED = 4,
    CRUX_IMAGE_SLOT_RUNNING_TESTING = 5,   /* on trial: confirm, or the next reset erases it */
    CRUX_IMAGE_SLOT_STANDBY_CONFIRMED = 6, /* valid, confirmed, not running: the fallback */
    CRUX_IMAGE_SLOT_TRIED = 7,             /* selected by the bootloader, not running */
    CRUX_IMAGE_SLOT_INVALID = 8            /* trailer holds a value outside the defined ones */
} crux_image_slot_state_t;

typedef struct {
    crux_image_slot_state_t state;
    crux_image_version_t version; /* meaningful unless state is EMPTY or INVALID */
} crux_image_slot_info_t;

typedef struct {
    crux_image_slot_t running_slot;
    bool running_confirmed; /* false: on trial */
    crux_image_version_t running_version;
    crux_image_slot_t inactive_slot; /* where the next image is written and requested */
    bool bootloader_version_known;
    crux_image_version_t bootloader_version;
    crux_image_slot_state_t slot_state_at_start[CRUX_IMAGE_SLOT_COUNT];
} crux_image_boot_record_t;

typedef struct { /* written by the update agent, cleared by it after the outcome is handled */
    crux_image_slot_t slot;
    crux_image_version_t version;
    bool requested; /* false while staging; set true just before crux_image_request_test() */
} crux_image_attempt_t;

typedef enum {
    CRUX_IMAGE_OUTCOME_NONE = 0,       /* no attempt, or a stale one */
    CRUX_IMAGE_OUTCOME_TESTING = 1,    /* the attempted image runs, unconfirmed */
    CRUX_IMAGE_OUTCOME_CONFIRMED = 2,  /* the attempted image runs, confirmed */
    CRUX_IMAGE_OUTCOME_NOT_BOOTED = 3, /* the request is still pending or was never made */
    CRUX_IMAGE_OUTCOME_REVERTED = 4    /* the attempted image is gone or refused */
} crux_image_outcome_t;

typedef enum {
    CRUX_IMAGE_OK = 0,
    CRUX_IMAGE_ERR_INVALID_ARGUMENT = -1,
    CRUX_IMAGE_ERR_NOT_READY = -2,    /* not started, or start failed */
    CRUX_IMAGE_ERR_NO_BOOT_DATA = -3, /* shared data or a needed key is missing */
    CRUX_IMAGE_ERR_BOOT_STATE = -4,   /* running trailer or mode is not supported */
    CRUX_IMAGE_ERR_IO = -5,
    CRUX_IMAGE_ERR_ACTIVE_SLOT = -6,    /* the request names the running slot */
    CRUX_IMAGE_ERR_NO_IMAGE = -7,       /* no valid header in the slot */
    CRUX_IMAGE_ERR_NOT_NEWER = -8,      /* the slot's version is not strictly newer */
    CRUX_IMAGE_ERR_SLOT_STATE = -9,     /* the slot is not STAGED, or PENDING_TEST (repeat) */
    CRUX_IMAGE_ERR_NOT_CONFIRMED = -10, /* the running image is on trial */
    CRUX_IMAGE_ERR_BUSY = -11
} crux_image_result_t;

crux_image_result_t crux_image_start(void);
crux_image_result_t crux_image_boot_record_get(crux_image_boot_record_t *record);
crux_image_result_t crux_image_slot_get(crux_image_slot_t slot, crux_image_slot_info_t *info);
crux_image_result_t crux_image_request_test(crux_image_slot_t slot);
crux_image_result_t crux_image_confirm(void);

/* Pure functions. No provider, no state. Arguments must not be NULL, except attempt. */
int crux_image_version_compare(const crux_image_version_t *a, const crux_image_version_t *b);
crux_image_outcome_t crux_image_outcome(const crux_image_boot_record_t *record,
                                        const crux_image_attempt_t *attempt /* may be NULL */);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_IMAGE_STATE_PROTO_H */
