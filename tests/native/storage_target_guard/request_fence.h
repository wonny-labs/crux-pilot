/* SPDX-License-Identifier: Apache-2.0 */
/* Unadopted sample-only NOR offsets; not a platform API or partition adoption. */
#ifndef CRUX_PILOT_REQUEST_FENCE_H
#define CRUX_PILOT_REQUEST_FENCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Caller-declared owner check; native dispatch does not infer caller identity. */
enum crux_pilot_flash_owner {
    CRUX_PILOT_FLASH_FILE,
    CRUX_PILOT_FLASH_SCRATCH,
    CRUX_PILOT_FLASH_NVS
};
static inline bool crux_pilot_flash_owner_request_valid(enum crux_pilot_flash_owner owner,
                                                        int64_t offset, size_t size, bool erase) {
    if (offset < 0 || size == 0) {
        return false;
    }
    uint64_t lower;
    uint64_t end;
    switch (owner) {
    case CRUX_PILOT_FLASH_FILE:
        lower = UINT64_C(0x620000);
        end = UINT64_C(0x7ef000);
        break;
    case CRUX_PILOT_FLASH_SCRATCH:
        lower = UINT64_C(0x7ef000);
        end = UINT64_C(0x7f0000);
        break;
    case CRUX_PILOT_FLASH_NVS:
        lower = UINT64_C(0x7f0000);
        end = UINT64_C(0x800000);
        break;
    default:
        return false;
    }
    uint64_t start = (uint64_t)offset;
    if (start < lower || start >= end || (uint64_t)size > end - start) {
        return false;
    }
    return !erase || (start % 4096U == 0 && size % 4096U == 0);
}

/* Firmware [0,0x620000) is denied. Each writable extent is checked separately.
 * The generic native driver guard admits any one storage owner, so File/NVS
 * callers still require an owner-bound flash-area composition above the driver.
 */
static inline bool crux_pilot_flash_request_valid(int64_t offset, size_t size, bool erase) {
    return crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_FILE, offset, size, erase) ||
           crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_SCRATCH, offset, size, erase) ||
           crux_pilot_flash_owner_request_valid(CRUX_PILOT_FLASH_NVS, offset, size, erase);
}
#endif
