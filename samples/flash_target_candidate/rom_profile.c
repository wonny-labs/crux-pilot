/* SPDX-License-Identifier: Apache-2.0 */
#include "rom_profile.h"
#include <fsl_clock.h>

bool crux_rom_read_confirm_supported(void) {
    /* NOR remains available. These are observations, not a measured SCK or a deadline.
     * The pinned getter includes CSCMR1[PODF]+1 exactly once; no extra divider. */
    struct crux_rom_profile p = {.cscmr1 = CCM->CSCMR1,
                                 .ccsr = CCM->CCSR,
                                 .ccgr6 = CCM->CCGR6,
                                 .pll_usb1 = CCM_ANALOG->PLL_USB1,
                                 .pll_sys = CCM_ANALOG->PLL_SYS,
                                 .pfd480 = CCM_ANALOG->PFD_480,
                                 .pfd528 = CCM_ANALOG->PFD_528,
                                 .sys_num = CCM_ANALOG->PLL_SYS_NUM,
                                 .sys_denom = CCM_ANALOG->PLL_SYS_DENOM,
                                 .mcr0 = FLEXSPI->MCR0,
                                 .mcr2 = FLEXSPI->MCR2,
                                 .sts0 = FLEXSPI->STS0,
                                 .flshcr1 = FLEXSPI->FLSHCR1[0],
                                 .flshcr2 = FLEXSPI->FLSHCR2[0]};
    if (!crux_rom_clock_inputs_supported(&p) || CLOCK_GetOscFreq() != 24000000U) {
        return false;
    }
    p.root_hz = CLOCK_GetClockRootFreq(kCLOCK_FlexspiClkRoot);
    /* Reject a changed clock observation before accepting the derived value. */
    if (p.cscmr1 != CCM->CSCMR1 || p.ccsr != CCM->CCSR || p.ccgr6 != CCM->CCGR6 ||
        p.pll_usb1 != CCM_ANALOG->PLL_USB1 || p.pll_sys != CCM_ANALOG->PLL_SYS ||
        p.pfd480 != CCM_ANALOG->PFD_480 || p.pfd528 != CCM_ANALOG->PFD_528 ||
        p.sys_num != CCM_ANALOG->PLL_SYS_NUM || p.sys_denom != CCM_ANALOG->PLL_SYS_DENOM) {
        return false;
    }
    for (unsigned i = 0; i < 4; ++i) {
        p.flshcr0[i] = FLEXSPI->FLSHCR0[i];
    }
    for (unsigned i = 0; i < 64; ++i) {
        p.lut[i] = FLEXSPI->LUT[i];
    }
    return crux_rom_profile_supported(&p);
}
