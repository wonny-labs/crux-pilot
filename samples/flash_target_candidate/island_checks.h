/* SPDX-License-Identifier: Apache-2.0 */
/* Private read-only harness checks; none authorizes a Flash operation. */
#ifndef CRUX_CANDIDATE_ISLAND_CHECKS_H
#define CRUX_CANDIDATE_ISLAND_CHECKS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
#endif
