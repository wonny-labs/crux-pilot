/* SPDX-License-Identifier: Apache-2.0 */
/* Private read-only harness checks; none authorizes a Flash operation. */
#ifndef CRUX_CANDIDATE_ISLAND_CHECKS_H
#define CRUX_CANDIDATE_ISLAND_CHECKS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Integer extents are checked before forming/dereferencing a byte range. ITCM
 * legitimately starts at zero; a blanket null-pointer check is incorrect. */
static inline bool crux_island_range_valid(uintptr_t start, size_t size, uintptr_t low,
                                           uintptr_t high, size_t alignment) {
    return start >= low && start < high && size != 0 && size <= high - start &&
           start % alignment == 0 && size % 4 == 0;
}
static inline bool crux_island_ranges_overlap(uintptr_t a, size_t a_size, uintptr_t b,
                                              size_t b_size) {
    /* Call only after both extents have been validated. */
    return a < b + b_size && b < a + a_size;
}
static inline bool crux_island_serial_root_supported(uint32_t cscmr1) {
    return (cscmr1 & 0x60000000U) != 0U;
}
static inline bool crux_island_snapshot_supported(const volatile uint32_t *s) {
    return s[0] == 0x50494c54U && s[1] == 1U && !(s[2] & 0x10000U) && !(s[3] & 1U) &&
           !((s[4] | s[6]) & 4U) && !((s[5] | s[7]) & 1U) && !(s[8] & 0x80U) && s[12] == 0U &&
           s[14] == 0U && s[15] == 0U;
}
static inline void crux_island_copy(volatile unsigned char *to, const volatile unsigned char *from,
                                    size_t size) {
    for (size_t i = 0; i < size; ++i) {
        to[i] = from[i];
    }
}
static inline bool crux_island_equal(const volatile unsigned char *a,
                                     const volatile unsigned char *b, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return true;
}
static inline bool crux_island_zero(const volatile unsigned char *a, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        if (a[i] != 0) {
            return false;
        }
    }
    return true;
}
#endif
