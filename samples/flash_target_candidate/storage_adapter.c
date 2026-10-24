/* SPDX-License-Identifier: Apache-2.0 */
#include "storage_adapter.h"
/* No retry/reset in one firmware boot; caller owns serialization and task context.
 * This outer adapter executes while NOR is ready. Only the copied per-command
 * bounce data enters the existing RAM island. No user pointer is consumed busy. */
static enum { COLD, ACTIVE, FAULTED, ENDED } state;
int crux_candidate_storage_begin(void) {
    if (state != COLD)
        return -EACCES;
    if (crux_candidate_command_prerequisites()) {
        state = FAULTED;
        return -EACCES;
    }
    state = ACTIVE;
    return 0;
}
bool crux_candidate_storage_ready(void) {
    return state == ACTIVE;
}
bool crux_candidate_storage_finish(void) {
    bool ready = state == ACTIVE;
    state = ENDED;
    return ready;
}
static int transfer(enum crux_pilot_flash_owner owner, int64_t offset, void *output,
                    const void *input, size_t length, enum crux_rom_operation operation) {
    if (state != ACTIVE)
        return -EACCES;
    if (!crux_pilot_flash_owner_request_valid(owner, offset, length, operation == CRUX_ROM_SECTOR))
        return -EACCES;
    if ((operation == CRUX_ROM_READ && !output) || (operation == CRUX_ROM_PROGRAM && !input))
        return -EINVAL;
    size_t done = 0;
    while (done < length) {
        size_t count = operation == CRUX_ROM_SECTOR ? 4096 : 256;
        if (operation == CRUX_ROM_PROGRAM)
            count -= (uint64_t)(offset + (int64_t)done) % 256;
        if (count > length - done)
            count = length - done;
        struct crux_rom_request request = {operation, owner, offset + (int64_t)done, count,
                                           input ? (const uint8_t *)input + done : NULL};
        int rc = crux_rom_command_submit(&request);
        if (!rc && operation == CRUX_ROM_READ)
            rc = crux_rom_command_read_result((uint8_t *)output + done, count);
        if (rc) {
            state = FAULTED;
            return rc; /* Earlier chunks may have completed. No rollback/full-success claim. */
        }
        done += count;
    }
    return 0;
}
int crux_candidate_read(enum crux_pilot_flash_owner owner, int64_t offset, void *bytes,
                        size_t len) {
    return transfer(owner, offset, bytes, NULL, len, CRUX_ROM_READ);
}
int crux_candidate_write(enum crux_pilot_flash_owner owner, int64_t offset, const void *bytes,
                         size_t len) {
    return transfer(owner, offset, NULL, bytes, len, CRUX_ROM_PROGRAM);
}
int crux_candidate_erase(enum crux_pilot_flash_owner owner, int64_t offset, size_t len) {
    return transfer(owner, offset, NULL, NULL, len, CRUX_ROM_SECTOR);
}
