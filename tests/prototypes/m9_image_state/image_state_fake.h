/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * Test controls of the Host double (kept apart from the contract header, as CONTRIBUTING.md
 * requires of test controls). The fake holds two slots, the bootloader's shared data, a write log
 * with failure injection, and a hand model of the bootloader's select and erase rules as the
 * proposal tabulates them ("What the bootloader leaves behind"). It is a test double, not a copy of
 * MCUboot.
 */
#ifndef CRUX_IMAGE_STATE_PROTO_FAKE_H
#define CRUX_IMAGE_STATE_PROTO_FAKE_H

#include "image_state_port.h"

typedef enum {
    FAKE_WRITE_MAGIC = 1,
    FAKE_WRITE_SWAP_INFO_TEST = 2,
    FAKE_WRITE_IMAGE_OK = 3
} fake_write_op_t;

/* Slot 0 holds a confirmed 1.0.0 image and runs. Slot 1 is empty. No fault is armed. */
void fake_init(void);

void fake_slot_put(crux_image_slot_t slot, const proto_slot_view_t *view);
proto_slot_view_t fake_slot_peek(crux_image_slot_t slot);
/* Whether the image passes validation after the bootloader loads it. Default true. */
void fake_slot_set_image_valid(crux_image_slot_t slot, bool valid);

void fake_shared_put(const proto_shared_data_t *shared);
void fake_shared_set_absent(bool absent);

/* The update agent's staging of an image: erase the whole slot, then write header and image. */
void fake_stage(crux_image_slot_t slot, crux_image_version_t version);

size_t fake_write_count(void);
fake_write_op_t fake_write_op(size_t index);
crux_image_slot_t fake_write_slot(size_t index);
/* The nth write (counted from this call, 1 is the next one) fails with an I/O error, once. */
void fake_fail_nth_write(size_t n);
/* While set, reading the trailer or header of the slot fails with an I/O error. */
void fake_fail_slot_read(crux_image_slot_t slot, bool fails);
/* Called at the start of every write, so that a test can re-enter the provider. */
void fake_set_write_hook(void (*hook)(void));
/* The bootloader's copy_done write fails (it only logs this) when set. */
void fake_set_copy_done_write_fails(bool fails);

/*
 * One reset of the board: the bootloader's select and erase rules, then new shared data. When no
 * slot can be booted the shared data reports no running slot.
 */
void fake_boot(void);

#endif /* CRUX_IMAGE_STATE_PROTO_FAKE_H */
