/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * The pure parts of the proposal: the version rule, the slot-state table and the outcome table.
 * No provider, no state, no storage.
 */
#include "image_state_port.h"

#include <stddef.h>

int crux_image_version_compare(const crux_image_version_t *a, const crux_image_version_t *b)
{
    /* Major, minor, revision, in that order. The build number is carried and never compared. */
    if (a->major != b->major) {
        return a->major < b->major ? -1 : 1;
    }
    if (a->minor != b->minor) {
        return a->minor < b->minor ? -1 : 1;
    }
    if (a->revision != b->revision) {
        return a->revision < b->revision ? -1 : 1;
    }
    return 0;
}

static bool same_release(const crux_image_version_t *a, const crux_image_version_t *b)
{
    return crux_image_version_compare(a, b) == 0;
}

crux_image_outcome_t crux_image_outcome(const crux_image_boot_record_t *record,
                                        const crux_image_attempt_t *attempt)
{
    /* Case 1 */
    if (record == NULL || attempt == NULL) {
        return CRUX_IMAGE_OUTCOME_NONE;
    }
    /* Prototype choice P9: an attempt on a slot that does not exist is no attempt. */
    if (attempt->slot >= CRUX_IMAGE_SLOT_COUNT) {
        return CRUX_IMAGE_OUTCOME_NONE;
    }
    if (attempt->slot == record->running_slot) {
        /* Case 2, and case 3 (stale: the slot was reused) */
        if (!same_release(&attempt->version, &record->running_version)) {
            return CRUX_IMAGE_OUTCOME_NONE;
        }
        return record->running_confirmed ? CRUX_IMAGE_OUTCOME_CONFIRMED
                                         : CRUX_IMAGE_OUTCOME_TESTING;
    }

    crux_image_slot_state_t state = record->slot_state_at_start[attempt->slot];
    /* Case 4: stale, the slot holds a confirmed image */
    if (state == CRUX_IMAGE_SLOT_STANDBY_CONFIRMED) {
        return CRUX_IMAGE_OUTCOME_NONE;
    }
    /* Case 5: the agent never asked for a boot */
    if (!attempt->requested) {
        return CRUX_IMAGE_OUTCOME_NOT_BOOTED;
    }
    switch (state) {
    case CRUX_IMAGE_SLOT_STAGED:
    case CRUX_IMAGE_SLOT_PENDING_TEST:
    case CRUX_IMAGE_SLOT_PENDING_PERMANENT:
        /* Case 6: the request was not consumed */
        return CRUX_IMAGE_OUTCOME_NOT_BOOTED;
    case CRUX_IMAGE_SLOT_EMPTY:
    case CRUX_IMAGE_SLOT_TRIED:
    case CRUX_IMAGE_SLOT_INVALID:
        /* Case 7: the image is gone or refused */
        return CRUX_IMAGE_OUTCOME_REVERTED;
    default:
        /* Prototype choice P9: a running state on the slot that is not running is not in the table.
         */
        return CRUX_IMAGE_OUTCOME_NONE;
    }
}

static bool flag_defined(proto_flag_t flag)
{
    return flag == PROTO_FLAG_UNSET || flag == PROTO_FLAG_SET;
}

crux_image_result_t proto_slot_state_derive(bool is_running, const proto_slot_view_t *view,
                                            crux_image_slot_state_t *state)
{
    /* Any slot with an invalid or erased header is EMPTY. */
    if (!view->header_valid) {
        *state = CRUX_IMAGE_SLOT_EMPTY;
        return is_running ? CRUX_IMAGE_ERR_BOOT_STATE : CRUX_IMAGE_OK;
    }

    if (is_running) {
        /* Under revert the bootloader erases a slot whose magic is not good (proposal, table). */
        if (view->magic != PROTO_MAGIC_GOOD) {
            *state = CRUX_IMAGE_SLOT_INVALID;
            return CRUX_IMAGE_ERR_BOOT_STATE;
        }
        if (view->image_ok == PROTO_FLAG_SET) {
            *state = CRUX_IMAGE_SLOT_RUNNING_CONFIRMED; /* copy_done: any */
            return CRUX_IMAGE_OK;
        }
        /* Prototype choice P3: the table does not define these running rows, so they fail closed.
         */
        if (view->image_ok != PROTO_FLAG_UNSET || !flag_defined(view->copy_done)) {
            *state = CRUX_IMAGE_SLOT_INVALID;
            return CRUX_IMAGE_ERR_BOOT_STATE;
        }
        /* image_ok unset: on trial, whether or not the bootloader's copy_done write landed. */
        *state = CRUX_IMAGE_SLOT_RUNNING_TESTING;
        return CRUX_IMAGE_OK;
    }

    /* A trailer value outside the defined ones, or a bad magic, is INVALID. Prototype choice P3
     * extends "a flag outside set and unset" to a slot whose magic is unset. */
    if (view->magic == PROTO_MAGIC_BAD || !flag_defined(view->image_ok) ||
        !flag_defined(view->copy_done)) {
        *state = CRUX_IMAGE_SLOT_INVALID;
        return CRUX_IMAGE_OK;
    }
    if (view->magic == PROTO_MAGIC_UNSET) {
        /*
         * STAGED needs an erased trailer. With image_ok set, a request that writes the magic would
         * make the slot PENDING_PERMANENT, which this contract never creates (I2). With copy_done
         * set it would read TRIED and be erased at the next reset. Prototype choice P3: INVALID.
         */
        bool erased = view->image_ok == PROTO_FLAG_UNSET && view->copy_done == PROTO_FLAG_UNSET;
        *state = erased ? CRUX_IMAGE_SLOT_STAGED : CRUX_IMAGE_SLOT_INVALID;
        return CRUX_IMAGE_OK;
    }
    if (view->image_ok == PROTO_FLAG_SET) {
        *state = view->copy_done == PROTO_FLAG_SET ? CRUX_IMAGE_SLOT_STANDBY_CONFIRMED
                                                   : CRUX_IMAGE_SLOT_PENDING_PERMANENT;
    } else {
        *state = view->copy_done == PROTO_FLAG_SET ? CRUX_IMAGE_SLOT_TRIED
                                                   : CRUX_IMAGE_SLOT_PENDING_TEST;
    }
    return CRUX_IMAGE_OK;
}
