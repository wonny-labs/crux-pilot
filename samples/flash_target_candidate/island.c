/* SPDX-License-Identifier: Apache-2.0 */
/* Conditional read-only context harness. Native storage always remains refused. */
#include "flash_poc.h"
#include "island_checks.h"
extern unsigned char __island_text_start[], __island_text_end[], __island_text_load[];
extern unsigned char __island_vectors_start[], __island_vectors_end[], __island_vectors_load[];
extern const volatile uint32_t pilot_reset_snapshot[16];
extern int crux_island_enter(void);

int crux_flash_island_refuse(void) {
    return -EACCES;
}

int crux_flash_island_inspect_and_refuse(void) {
    /* All of this runs while NOR is unchanged, before the RAM-context switch. */
    if (!crux_island_snapshot_supported(pilot_reset_snapshot)) {
        return -EACCES;
    }
    size_t text_size = (size_t)(__island_text_end - __island_text_start);
    size_t vectors_size = (size_t)(__island_vectors_end - __island_vectors_start);
    crux_island_copy(__island_text_start, __island_text_load, text_size);
    crux_island_copy(__island_vectors_start, __island_vectors_load, vectors_size);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    if (!crux_island_equal(__island_text_start, __island_text_load, text_size) ||
        !crux_island_equal(__island_vectors_start, __island_vectors_load, vectors_size)) {
        return -EACCES;
    }
    /* Return establishes only a completed unchanged read-only observation. */
    (void)crux_island_enter();
    return -EACCES;
}
