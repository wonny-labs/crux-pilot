/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Private seam between the prototype provider and its medium. In the proposal the medium is
 * bootutil over flash_area_* plus the bootloader's shared data. Here the only implementation is the
 * fake in image_state_fake.c. It carries no bootutil type.
 */
#ifndef CRUX_IMAGE_STATE_PROTO_PORT_H
#define CRUX_IMAGE_STATE_PROTO_PORT_H

#include "image_state_proto.h"

typedef enum { PROTO_MAGIC_UNSET = 0, PROTO_MAGIC_GOOD = 1, PROTO_MAGIC_BAD = 2 } proto_magic_t;

/* A trailer flag is unset, set, or holds a value outside the defined ones. */
typedef enum { PROTO_FLAG_UNSET = 0, PROTO_FLAG_SET = 1, PROTO_FLAG_INVALID = 2 } proto_flag_t;

typedef struct {
    bool header_valid;
    crux_image_version_t version;
    proto_magic_t magic;
    proto_flag_t image_ok;
    proto_flag_t copy_done;
} proto_slot_view_t;

typedef enum { PROTO_MODE_MISSING = 0, PROTO_MODE_RAM_LOAD = 1, PROTO_MODE_OTHER = 2 } proto_mode_t;

typedef struct {
    bool running_slot_present;
    uint32_t running_slot; /* as the bootloader reports it, so a value above 1 can be tested */
    proto_mode_t mode;
    bool bootloader_version_present;
    crux_image_version_t bootloader_version;
} proto_shared_data_t;

/* Returns false when the shared data is absent altogether. */
bool proto_port_shared_data_get(proto_shared_data_t *out);
crux_image_result_t proto_port_slot_read(crux_image_slot_t slot, proto_slot_view_t *view);
/* The request writes, in the stock order of the proposal: magic, then swap info (test type). */
crux_image_result_t proto_port_write_magic(crux_image_slot_t slot);
crux_image_result_t proto_port_write_swap_info_test(crux_image_slot_t slot);
/* Confirm writes image_ok of one slot. */
crux_image_result_t proto_port_write_image_ok(crux_image_slot_t slot);

/*
 * Derives a slot's state from its header and trailer (the slot-state table of the proposal).
 * Returns CRUX_IMAGE_OK, or CRUX_IMAGE_ERR_BOOT_STATE for a running slot whose trailer the
 * proposal says cannot be running (magic not good) or does not define (see prototype-notes.md).
 */
crux_image_result_t proto_slot_state_derive(bool is_running, const proto_slot_view_t *view,
                                            crux_image_slot_state_t *state);

/* Test control: forget the cached start so the next crux_image_start() reads the medium again. */
void proto_provider_reset(void);

#endif /* CRUX_IMAGE_STATE_PROTO_PORT_H */
