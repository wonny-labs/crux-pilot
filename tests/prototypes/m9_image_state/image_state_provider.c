/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * The provider of the proposal's Behaviour section over the private port. It fails closed (I6): it
 * never guesses the running slot, and a failed start leaves it not ready. There is no lock: a
 * single flag stands in for the proposal's internal lock (prototype choice P5).
 */
#include "image_state_port.h"

#include <stddef.h>

static bool started;
static bool busy;
static crux_image_boot_record_t cached;

void proto_provider_reset(void)
{
    started = false;
    busy = false;
    cached = (crux_image_boot_record_t){0};
}

static bool slot_valid(crux_image_slot_t slot)
{
    return slot < CRUX_IMAGE_SLOT_COUNT;
}

/* Reads and classifies one slot from the live trailer. */
static crux_image_result_t read_state(crux_image_slot_t slot, proto_slot_view_t *view,
                                      crux_image_slot_state_t *state)
{
    crux_image_result_t result = proto_port_slot_read(slot, view);
    if (result != CRUX_IMAGE_OK) {
        return result;
    }
    bool running = started && slot == cached.running_slot;
    return proto_slot_state_derive(running, view, state);
}

crux_image_result_t crux_image_start(void)
{
    if (started) {
        return CRUX_IMAGE_OK; /* B4: the record is cached and nothing changes */
    }
    proto_shared_data_t shared;
    if (!proto_port_shared_data_get(&shared) || !shared.running_slot_present ||
        shared.mode == PROTO_MODE_MISSING) {
        return CRUX_IMAGE_ERR_NO_BOOT_DATA;
    }
    if (shared.mode != PROTO_MODE_RAM_LOAD || shared.running_slot >= CRUX_IMAGE_SLOT_COUNT) {
        return CRUX_IMAGE_ERR_BOOT_STATE;
    }

    crux_image_boot_record_t record = {0};
    record.running_slot = (crux_image_slot_t)shared.running_slot;
    record.inactive_slot = (crux_image_slot_t)(1u - record.running_slot);
    record.bootloader_version_known = shared.bootloader_version_present;
    if (shared.bootloader_version_present) {
        record.bootloader_version = shared.bootloader_version;
    }

    for (crux_image_slot_t slot = 0; slot < CRUX_IMAGE_SLOT_COUNT; slot++) {
        proto_slot_view_t view;
        crux_image_slot_state_t state;
        crux_image_result_t result = proto_port_slot_read(slot, &view);
        if (result != CRUX_IMAGE_OK) {
            return result;
        }
        result = proto_slot_state_derive(slot == record.running_slot, &view, &state);
        if (result != CRUX_IMAGE_OK) {
            return result; /* the running slot is not in a state that this contract supports */
        }
        record.slot_state_at_start[slot] = state;
        if (slot == record.running_slot) {
            record.running_version = view.version;
            record.running_confirmed = state == CRUX_IMAGE_SLOT_RUNNING_CONFIRMED;
        }
    }
    cached = record;
    started = true;
    return CRUX_IMAGE_OK;
}

crux_image_result_t crux_image_boot_record_get(crux_image_boot_record_t *record)
{
    if (record == NULL) {
        return CRUX_IMAGE_ERR_INVALID_ARGUMENT;
    }
    if (!started) {
        return CRUX_IMAGE_ERR_NOT_READY;
    }
    *record = cached;
    return CRUX_IMAGE_OK;
}

crux_image_result_t crux_image_slot_get(crux_image_slot_t slot, crux_image_slot_info_t *info)
{
    if (info == NULL || !slot_valid(slot)) {
        return CRUX_IMAGE_ERR_INVALID_ARGUMENT;
    }
    if (!started) {
        return CRUX_IMAGE_ERR_NOT_READY;
    }
    proto_slot_view_t view;
    crux_image_slot_state_t state;
    crux_image_result_t result = read_state(slot, &view, &state);
    if (result == CRUX_IMAGE_ERR_BOOT_STATE) {
        /* A live trailer that is no longer one this contract supports is reported, not hidden. */
        state = CRUX_IMAGE_SLOT_INVALID;
    } else if (result != CRUX_IMAGE_OK) {
        return result;
    }
    info->state = state;
    info->version = view.version;
    return CRUX_IMAGE_OK;
}

/* The checks of crux_image_request_test(), in the order of the proposal. */
static crux_image_result_t request_checks(crux_image_slot_t slot, crux_image_slot_state_t *state)
{
    if (!slot_valid(slot)) {
        return CRUX_IMAGE_ERR_INVALID_ARGUMENT;
    }
    if (slot == cached.running_slot) {
        return CRUX_IMAGE_ERR_ACTIVE_SLOT;
    }

    proto_slot_view_t running_view;
    crux_image_slot_state_t running_state;
    crux_image_result_t result = read_state(cached.running_slot, &running_view, &running_state);
    if (result != CRUX_IMAGE_OK) {
        return result;
    }
    if (running_state != CRUX_IMAGE_SLOT_RUNNING_CONFIRMED) {
        return CRUX_IMAGE_ERR_NOT_CONFIRMED;
    }

    proto_slot_view_t view;
    result = read_state(slot, &view, state);
    if (result != CRUX_IMAGE_OK) {
        return result;
    }
    if (!view.header_valid) {
        return CRUX_IMAGE_ERR_NO_IMAGE;
    }
    /* The running image's version comes from its own header in flash, not from the cache. */
    if (crux_image_version_compare(&view.version, &running_view.version) <= 0) {
        return CRUX_IMAGE_ERR_NOT_NEWER;
    }
    if (*state != CRUX_IMAGE_SLOT_STAGED && *state != CRUX_IMAGE_SLOT_PENDING_TEST) {
        return CRUX_IMAGE_ERR_SLOT_STATE;
    }
    return CRUX_IMAGE_OK;
}

crux_image_result_t crux_image_request_test(crux_image_slot_t slot)
{
    if (!started) {
        return CRUX_IMAGE_ERR_NOT_READY;
    }
    if (busy) {
        return CRUX_IMAGE_ERR_BUSY;
    }
    busy = true;

    crux_image_slot_state_t state = CRUX_IMAGE_SLOT_EMPTY;
    crux_image_result_t result = request_checks(slot, &state);
    if (result == CRUX_IMAGE_OK && state == CRUX_IMAGE_SLOT_STAGED) {
        /* The stock order: the magic, then the swap info. No image_ok: this is a test request. */
        result = proto_port_write_magic(slot);
        if (result == CRUX_IMAGE_OK) {
            result = proto_port_write_swap_info_test(slot);
        }
    }
    /* A repeat for a PENDING_TEST slot returns OK without writing. */
    busy = false;
    return result;
}

crux_image_result_t crux_image_confirm(void)
{
    if (!started) {
        return CRUX_IMAGE_ERR_NOT_READY;
    }
    if (busy) {
        return CRUX_IMAGE_ERR_BUSY;
    }
    busy = true;

    proto_slot_view_t view;
    crux_image_slot_state_t state;
    crux_image_result_t result = read_state(cached.running_slot, &view, &state);
    if (result == CRUX_IMAGE_OK && state == CRUX_IMAGE_SLOT_RUNNING_TESTING) {
        /* The running slot only: confirm never names a slot. */
        result = proto_port_write_image_ok(cached.running_slot);
    }
    /* Already confirmed: OK without a write. Any other live state is the ERR_BOOT_STATE of read. */
    busy = false;
    return result;
}
