/* SPDX-License-Identifier: Apache-2.0 */
/* Unadopted sample-only NOR offsets; not a platform API or partition adoption. */
#ifndef CRUX_PILOT_REQUEST_FENCE_H
#define CRUX_PILOT_REQUEST_FENCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
static inline bool crux_pilot_flash_request_valid(int64_t offset, size_t size, bool erase) {
    if (offset < 0 || size == 0) {
        return false;
    }
    uint64_t start = (uint64_t)offset;
    uint64_t end;
    if (start >= UINT64_C(0x620000) && start < UINT64_C(0x7f0000)) {
        end = UINT64_C(0x7f0000);
    } else if (start >= UINT64_C(0x7f0000) && start < UINT64_C(0x800000)) {
        end = UINT64_C(0x800000);
    } else {
        return false;
    }
    if ((uint64_t)size > end - start) {
        return false;
    }
    return !erase || (start % 4096U == 0 && size % 4096U == 0);
}
#endif
