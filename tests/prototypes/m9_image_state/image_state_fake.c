/*
 * PROTOTYPE, NOT ADOPTED, NOT FOR MERGE.
 * Exists so the team can review the proposed image-state contract in running code.
 *
 * The Host double: a fake medium and a hand model of the bootloader (see image_state_fake.h).
 * The model's version order is written out here on purpose, and does not call the provider's
 * crux_image_version_compare(), so that the two can disagree and a test can see it.
 */
#include "image_state_fake.h"

#include <string.h>

#define FAKE_LOG_CAPACITY 16u

typedef struct {
    fake_write_op_t op;
    crux_image_slot_t slot;
} fake_log_entry_t;

static proto_slot_view_t slots[CRUX_IMAGE_SLOT_COUNT];
static bool image_valid[CRUX_IMAGE_SLOT_COUNT];
static proto_shared_data_t shared;
static bool shared_absent;
static fake_log_entry_t log_entries[FAKE_LOG_CAPACITY];
static size_t log_count;
static size_t writes_until_failure;
static void (*write_hook)(void);
static bool copy_done_write_fails;
static bool read_fails[CRUX_IMAGE_SLOT_COUNT];

void fake_init(void)
{
    memset(slots, 0, sizeof(slots));
    slots[0].header_valid = true;
    slots[0].version = (crux_image_version_t){1, 0, 0, 0};
    slots[0].magic = PROTO_MAGIC_GOOD;
    slots[0].image_ok = PROTO_FLAG_SET;
    slots[0].copy_done = PROTO_FLAG_SET;
    image_valid[0] = true;
    image_valid[1] = true;
    shared = (proto_shared_data_t){true, 0, PROTO_MODE_RAM_LOAD, true, {2, 4, 0, 0}};
    shared_absent = false;
    memset(log_entries, 0, sizeof(log_entries));
    log_count = 0;
    writes_until_failure = 0;
    write_hook = NULL;
    copy_done_write_fails = false;
    memset(read_fails, 0, sizeof(read_fails));
    proto_provider_reset();
}

void fake_slot_put(crux_image_slot_t slot, const proto_slot_view_t *view)
{
    slots[slot] = *view;
}

proto_slot_view_t fake_slot_peek(crux_image_slot_t slot)
{
    return slots[slot];
}

void fake_slot_set_image_valid(crux_image_slot_t slot, bool valid)
{
    image_valid[slot] = valid;
}

void fake_shared_put(const proto_shared_data_t *data)
{
    shared = *data;
}

void fake_shared_set_absent(bool absent)
{
    shared_absent = absent;
}

static void erase(crux_image_slot_t slot)
{
    memset(&slots[slot], 0, sizeof(slots[slot]));
    image_valid[slot] = true;
}

void fake_stage(crux_image_slot_t slot, crux_image_version_t version)
{
    erase(slot);
    slots[slot].header_valid = true;
    slots[slot].version = version;
}

size_t fake_write_count(void)
{
    return log_count;
}

fake_write_op_t fake_write_op(size_t index)
{
    return log_entries[index].op;
}

crux_image_slot_t fake_write_slot(size_t index)
{
    return log_entries[index].slot;
}

void fake_fail_nth_write(size_t n)
{
    writes_until_failure = n;
}

void fake_fail_slot_read(crux_image_slot_t slot, bool fails)
{
    read_fails[slot] = fails;
}

void fake_set_write_hook(void (*hook)(void))
{
    write_hook = hook;
}

void fake_set_copy_done_write_fails(bool fails)
{
    copy_done_write_fails = fails;
}

/* Logs the write, and says whether it lands. A failed write changes nothing and is not logged. */
static bool write_lands(fake_write_op_t op, crux_image_slot_t slot)
{
    if (write_hook != NULL) {
        write_hook();
    }
    if (writes_until_failure != 0 && --writes_until_failure == 0) {
        return false;
    }
    if (log_count < FAKE_LOG_CAPACITY) {
        log_entries[log_count].op = op;
        log_entries[log_count].slot = slot;
        log_count++;
    }
    return true;
}

bool proto_port_shared_data_get(proto_shared_data_t *out)
{
    if (shared_absent) {
        return false;
    }
    *out = shared;
    return true;
}

crux_image_result_t proto_port_slot_read(crux_image_slot_t slot, proto_slot_view_t *view)
{
    if (read_fails[slot]) {
        return CRUX_IMAGE_ERR_IO;
    }
    *view = slots[slot];
    return CRUX_IMAGE_OK;
}

crux_image_result_t proto_port_write_magic(crux_image_slot_t slot)
{
    if (!write_lands(FAKE_WRITE_MAGIC, slot)) {
        return CRUX_IMAGE_ERR_IO;
    }
    slots[slot].magic = PROTO_MAGIC_GOOD;
    return CRUX_IMAGE_OK;
}

crux_image_result_t proto_port_write_swap_info_test(crux_image_slot_t slot)
{
    if (!write_lands(FAKE_WRITE_SWAP_INFO_TEST, slot)) {
        return CRUX_IMAGE_ERR_IO;
    }
    return CRUX_IMAGE_OK;
}

crux_image_result_t proto_port_write_image_ok(crux_image_slot_t slot)
{
    if (!write_lands(FAKE_WRITE_IMAGE_OK, slot)) {
        return CRUX_IMAGE_ERR_IO;
    }
    slots[slot].image_ok = PROTO_FLAG_SET;
    return CRUX_IMAGE_OK;
}

/* Strictly greater on major, minor, revision. The build number is ignored. */
static bool model_newer(const crux_image_version_t *a, const crux_image_version_t *b)
{
    if (a->major != b->major) {
        return a->major > b->major;
    }
    if (a->minor != b->minor) {
        return a->minor > b->minor;
    }
    return a->revision > b->revision;
}

void fake_boot(void)
{
    bool tried[CRUX_IMAGE_SLOT_COUNT] = {false, false};
    bool booted = false;
    uint32_t running = 0;

    while (!booted) {
        /* The available slot with the highest version. On equal versions the lower slot wins. */
        int best = -1;
        for (crux_image_slot_t s = 0; s < CRUX_IMAGE_SLOT_COUNT; s++) {
            if (!slots[s].header_valid || tried[s]) {
                continue;
            }
            if (best < 0 || model_newer(&slots[s].version, &slots[best].version)) {
                best = s;
            }
        }
        if (best < 0) {
            break;
        }
        crux_image_slot_t slot = (crux_image_slot_t)best;
        tried[slot] = true;
        /* Select or erase, before the image is loaded or validated. */
        if (slots[slot].magic != PROTO_MAGIC_GOOD ||
            (slots[slot].copy_done == PROTO_FLAG_SET && slots[slot].image_ok != PROTO_FLAG_SET)) {
            erase(slot);
            continue;
        }
        if (!copy_done_write_fails) {
            slots[slot].copy_done = PROTO_FLAG_SET;
        }
        /* A validation failure only makes the slot unavailable for this boot. */
        if (!image_valid[slot]) {
            continue;
        }
        running = slot;
        booted = true;
    }
    shared.running_slot_present = booted;
    shared.running_slot = running;
}
