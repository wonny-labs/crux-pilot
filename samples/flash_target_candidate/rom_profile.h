/* SPDX-License-Identifier: Apache-2.0 */
/* Private read-confirmation only. Success never authorizes media dispatch. */
#ifndef CRUX_CANDIDATE_ROM_PROFILE_H
#define CRUX_CANDIDATE_ROM_PROFILE_H
#include <stdbool.h>
#include <stdint.h>
struct crux_rom_profile {
    uint32_t cscmr1, ccsr, ccgr6, pll_usb1, pll_sys, pfd480, pfd528, sys_num, sys_denom;
    uint32_t mcr0, mcr2, sts0, flshcr0[4], flshcr1, flshcr2, root_hz, lut[64];
};
bool crux_rom_read_confirm_supported(void);
static inline bool crux_rom_clock_inputs_supported(const struct crux_rom_profile *p) {
    unsigned selector = (p->cscmr1 >> 29) & 3U;
    if (selector == 0 || (p->ccgr6 & 0xc00U) != 0xc00U) {
        return false;
    }
    /* Reject bypass, unknown reference, disabled/unlocked PLL and gated/invalid PFD.
     * The pinned SDK getter does not check PFD gating or protect a zero divisor. */
    if (selector == 2) {
        unsigned pfd = (p->pfd528 >> 16) & 0xffU;
        return (p->pll_sys & 0x8001f000U) == 0x80002000U && p->sys_num == 0 && p->sys_denom != 0 &&
               (pfd & 0xc0U) == 0x40U && (pfd & 0x3fU) >= 12 && (pfd & 0x3fU) <= 35;
    }
    if ((p->pll_usb1 & 0x8001f000U) != 0x80003000U) {
        return false;
    }
    if (selector == 1) {
        return !(p->ccsr & 1U); /* PLL3_SW_CLK_SEL: require PLL, not oscillator bypass. */
    }
    unsigned pfd = p->pfd480 & 0xffU;
    return (pfd & 0xc0U) == 0x40U && (pfd & 0x3fU) >= 12 && (pfd & 0x3fU) <= 35;
}
static inline bool crux_rom_profile_supported(const struct crux_rom_profile *p) {
    if (!crux_rom_clock_inputs_supported(p) || !p->root_hz || p->root_hz > 104000000U ||
        (p->mcr0 & 0x6033U) != 0x10U || (p->mcr2 & 0x88000U) || (p->sts0 & 3U) != 3U ||
        p->flshcr0[0] != 8192U || p->flshcr0[1] || p->flshcr0[2] || p->flshcr0[3] ||
        (p->flshcr1 & 0x7fffU) != 0x63U || (p->flshcr2 & 0x800000efU)) {
        return false;
    }
    /* ROM FCB READ 0, status 1, WREN 3, sector erase 5, page program 9.
     * Compare complete selected sequences including STOP tails; do not install 9Fh. */
    static const unsigned slots[] = {0, 1, 3, 5, 9};
    static const uint32_t expected[][4] = {{0x0a1804ebU, 0x26043206U, 0, 0},
                                           {0x24040405U, 0, 0, 0},
                                           {0x00000406U, 0, 0, 0},
                                           {0x08180420U, 0, 0, 0},
                                           {0x08180402U, 0x00002004U, 0, 0}};
    for (unsigned s = 0; s < 5; ++s) {
        for (unsigned word = 0; word < 4; ++word) {
            if (p->lut[4 * slots[s] + word] != expected[s][word]) {
                return false;
            }
        }
    }
    return true;
}
#endif
