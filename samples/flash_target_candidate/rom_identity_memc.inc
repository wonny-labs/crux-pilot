/* SPDX-License-Identifier: Apache-2.0 */
/* Included only in the isolated native MEMC unit; never a normal-init path. */
#include "rom_deadline.h"
/* Exact pinned EVKB-B FCB lookup_table at offset 0x80. Full 64-word witness,
 * including block/chip erase and every zero/STOP tail, not just used commands. */
static const uint32_t crux_rom_expected_lut[64] = {
    [0] = 0x0a1804ebU,  [1] = 0x26043206U,  [4] = 0x24040405U,
    [12] = 0x00000406U, [20] = 0x08180420U, [32] = 0x081804d8U,
    [36] = 0x08180402U, [37] = 0x00002004U, [44] = 0x00000460U,
};
/* Slot 15 is fixed only for this exclusive image profile. The actual native
 * helper reads three bytes; SDK copies four owned words and locks afterwards. */
static const uint32_t crux_rom_id_lut[4] = {0x2401049fU, 0, 0, 0};
static struct {
    bool installed, captured;
    uint32_t saved[4], saved_lock;
    uint32_t mcr0, mcr2, flshcr1, flshcr2;
} crux_rom_slot;

bool crux_rom_slot_installed(void) {
    return crux_rom_slot.installed;
}
void crux_rom_identity_check(const struct device *controller, bool installed) {
    FLEXSPI_Type *base = get_base(controller);
    crux_rom_candidate_wait_check();
    /* STS0[ARBIDLE,SEQIDLE] makes stale IPCR1 harmless. An active transfer
     * refuses regardless of its start/count or target port; no active-IP guess. */
    if (installed != crux_rom_slot.installed || base->LUTCR != 1U || (base->STS0 & 3U) != 3U ||
        (base->MCR0 & 0x6033U) != 0x10U || (base->MCR2 & 0x88000U) ||
        (base->FLSHCR1[0] & 0x7fffU) != 0x63U || base->FLSHCR0[0] != 8192U) {
        crux_flash_poc_terminal(-EACCES);
    }
    for (unsigned port = 0; port < 4; ++port) {
        uint32_t seq = base->FLSHCR2[port];
        unsigned read = seq & 15U, read_count = ((seq >> 5) & 7U) + 1U;
        unsigned write = (seq >> 8) & 15U, write_count = ((seq >> 13) & 7U) + 1U;
        /* Sole A1 AHB chain is READ0/count1; AHB write also selects 0/count1.
         * Neither chain may include 15 or wrap; no enabled other-port extent. */
        if ((port && (base->FLSHCR0[port] || seq)) || read + read_count > 15U ||
            write + write_count > 15U || (!port && (seq & 0x8000efefU))) {
            crux_flash_poc_terminal(-EACCES);
        }
    }
    if (crux_rom_slot.captured &&
        (base->MCR0 != crux_rom_slot.mcr0 || base->MCR2 != crux_rom_slot.mcr2 ||
         base->FLSHCR1[0] != crux_rom_slot.flshcr1 || base->FLSHCR2[0] != crux_rom_slot.flshcr2)) {
        crux_flash_poc_terminal(-EACCES);
    }
    for (unsigned word = 0; word < 64; ++word) {
        uint32_t expected =
            installed && word >= 60 ? crux_rom_id_lut[word - 60] : crux_rom_expected_lut[word];
        if (base->LUT[word] != expected) {
            /* Unknown/unowned changes are terminal; never repair globally. */
            crux_flash_poc_terminal(-EACCES);
        }
    }
}
void crux_rom_identity_install(const struct device *controller) {
    FLEXSPI_Type *base = get_base(controller);
    crux_rom_identity_check(controller, false);
    crux_rom_slot.saved_lock = base->LUTCR;
    crux_rom_slot.mcr0 = base->MCR0;
    crux_rom_slot.mcr2 = base->MCR2;
    crux_rom_slot.flshcr1 = base->FLSHCR1[0];
    crux_rom_slot.flshcr2 = base->FLSHCR2[0];
    for (unsigned i = 0; i < 4; ++i) {
        crux_rom_slot.saved[i] = base->LUT[60 + i];
    }
    crux_rom_slot.captured = true;
    FLEXSPI_UpdateLUT(base, 60, crux_rom_id_lut, 4);
    crux_rom_slot.installed = true;
    crux_rom_identity_check(controller, true);
}
void crux_rom_identity_restore(const struct device *controller) {
    FLEXSPI_Type *base = get_base(controller);
    /* Caller has positive NOR-ready + controller idle; verify installed state
     * again before touching the key/slot. LOCKED is the only supported state.
     * Actual LUTCR readback semantics and initial state remain unqualified. */
    crux_rom_identity_check(controller, true);
    if (crux_rom_slot.saved_lock != 1U) {
        crux_flash_poc_terminal(-EACCES);
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (crux_rom_slot.saved[i] != crux_rom_expected_lut[60 + i]) {
            crux_flash_poc_terminal(-EACCES);
        }
    }
    FLEXSPI_UpdateLUT(base, 60, crux_rom_slot.saved, 4);
    crux_rom_slot.installed = false;
    crux_rom_identity_check(controller, false);
}
