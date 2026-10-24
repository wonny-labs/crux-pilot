/* SPDX-License-Identifier: Apache-2.0 */
/* Conditional read-only context harness. Native storage always remains refused. */
#include "flash_poc.h"
#include "island_checks.h"
#include "rom_profile.h"
#include <zephyr/init.h>
extern unsigned char __island_text_start[], __island_text_end[], __island_text_load[];
extern unsigned char __island_vectors_start[], __island_vectors_end[], __island_vectors_load[];
extern unsigned char __island_operation_bottom[], __island_exception_top[];
extern unsigned char __itcm_text_reloc_start[], __itcm_text_rom_start[], __itcm_text_reloc_size[];
extern unsigned char __dtcm_rodata_reloc_start[], __dtcm_rodata_rom_start[],
    __dtcm_rodata_reloc_size[];
extern unsigned char __dtcm_data_reloc_start[], __dtcm_data_rom_start[], __dtcm_data_reloc_size[];
extern unsigned char __dtcm_bss_reloc_start[], __dtcm_bss_reloc_size[];
extern const volatile uint32_t pilot_reset_snapshot[16];
extern int crux_island_enter(void);

static enum {
    CRUX_RELOCATION_UNCHECKED,
    CRUX_RELOCATION_READY,
    CRUX_RELOCATION_REFUSED
} crux_relocation_phase;

static bool crux_island_relocations_valid(bool initial) {
    /* Fixed sample sections, not a caller-supplied loader/manifest. Zephyr has
     * already copied the first three and zeroed BSS before z_cstart. */
    const struct {
        uintptr_t ram, load;
        size_t size;
    } regions[] = {
        {(uintptr_t)__itcm_text_reloc_start, (uintptr_t)__itcm_text_rom_start,
         (size_t)__itcm_text_reloc_size},
        {(uintptr_t)__dtcm_rodata_reloc_start, (uintptr_t)__dtcm_rodata_rom_start,
         (size_t)__dtcm_rodata_reloc_size},
        {(uintptr_t)__dtcm_data_reloc_start, (uintptr_t)__dtcm_data_rom_start,
         (size_t)__dtcm_data_reloc_size},
        {(uintptr_t)__dtcm_bss_reloc_start, 0, (size_t)__dtcm_bss_reloc_size},
        {(uintptr_t)__island_text_start, (uintptr_t)__island_text_load,
         (uintptr_t)__island_text_end - (uintptr_t)__island_text_start},
        {(uintptr_t)__island_vectors_start, (uintptr_t)__island_vectors_load,
         (uintptr_t)__island_vectors_end - (uintptr_t)__island_vectors_start},
    };
    uintptr_t stacks = (uintptr_t)__island_operation_bottom;
    size_t stacks_size = (uintptr_t)__island_exception_top - stacks;
    if (!crux_island_range_valid(stacks, stacks_size, 0x20000000U, 0x20020000U, 8) ||
        stacks_size != 2048 || regions[5].size != 1024) {
        return false;
    }
    /* Validate every descriptor before the first byte access, including the
     * separate assembly/vectors that preflight will subsequently copy. */
    for (size_t i = 0; i < 6; ++i) {
        bool code = i == 0 || i == 4;
        if (!crux_island_range_valid(regions[i].ram, regions[i].size, code ? 0 : 0x20000000U,
                                     code ? 0x20000U : 0x20020000U, i == 5 ? 1024 : 4) ||
            (i != 3 && !crux_island_range_valid(regions[i].load, regions[i].size, 0x60000000U,
                                                0x60620000U, 4))) {
            return false;
        }
    }
    for (size_t i = 0; i < 6; ++i) {
        if (crux_island_ranges_overlap(regions[i].ram, regions[i].size, stacks, stacks_size)) {
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (crux_island_ranges_overlap(regions[i].ram, regions[i].size, regions[j].ram,
                                           regions[j].size) ||
                (i != 3 && j != 3 &&
                 crux_island_ranges_overlap(regions[i].load, regions[i].size, regions[j].load,
                                            regions[j].size))) {
                return false;
            }
        }
    }
    for (size_t i = 0; i < (initial ? 3U : 2U); ++i) {
        if (!crux_island_equal((const volatile unsigned char *)regions[i].ram,
                               (const volatile unsigned char *)regions[i].load, regions[i].size)) {
            return false;
        }
    }
    return !initial || crux_island_zero(__dtcm_bss_reloc_start, (size_t)__dtcm_bss_reloc_size);
}

static int crux_island_relocation_init(void) {
    /* Supported boot only: arch_bss_zero/arch_data_copy finish before z_cstart;
     * PRE_KERNEL_1 precedes both native POST_KERNEL entry points and private
     * preparation (which has no target caller). Never repair mutable bytes.
     * An early entry, failed check, or repeated initialization stays refused. */
    if (crux_relocation_phase != CRUX_RELOCATION_UNCHECKED ||
        !crux_island_relocations_valid(true)) {
        crux_relocation_phase = CRUX_RELOCATION_REFUSED;
        return -EACCES;
    }
    crux_relocation_phase = CRUX_RELOCATION_READY;
    return 0;
}
SYS_INIT(crux_island_relocation_init, PRE_KERNEL_1, 0);

int crux_flash_island_refuse(void) {
    return -EACCES;
}

int crux_flash_island_inspect_and_refuse(void) {
    /* All of this runs while NOR is unchanged, before the RAM-context switch. */
    if (crux_relocation_phase != CRUX_RELOCATION_READY) {
        crux_relocation_phase = CRUX_RELOCATION_REFUSED;
        return -EACCES;
    }
    if (!crux_island_snapshot_supported(pilot_reset_snapshot) ||
        !crux_rom_read_confirm_supported()) {
        return -EACCES;
    }
    if (!crux_island_relocations_valid(false)) {
        crux_relocation_phase = CRUX_RELOCATION_REFUSED;
        return -EACCES;
    }
    size_t text_size = (uintptr_t)__island_text_end - (uintptr_t)__island_text_start;
    size_t vectors_size = (uintptr_t)__island_vectors_end - (uintptr_t)__island_vectors_start;
    crux_island_copy(__island_text_start, __island_text_load, text_size);
    crux_island_copy(__island_vectors_start, __island_vectors_load, vectors_size);
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    if (!crux_island_equal(__island_text_start, __island_text_load, text_size) ||
        !crux_island_equal(__island_vectors_start, __island_vectors_load, vectors_size)) {
        crux_relocation_phase = CRUX_RELOCATION_REFUSED;
        return -EACCES;
    }
    /* Return establishes only a completed unchanged read-only observation. */
    (void)crux_island_enter();
    return -EACCES;
}
