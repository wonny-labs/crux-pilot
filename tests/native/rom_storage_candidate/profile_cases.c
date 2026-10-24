/* SPDX-License-Identifier: Apache-2.0 */
#include "rom_profile.h"
#include <assert.h>
int main(void) {
    struct crux_rom_profile p = {.cscmr1 = 3U << 29,
                                 .ccgr6 = 3U << 10,
                                 .pll_usb1 = 0x80003000U,
                                 .pfd480 = 0x58U,
                                 .mcr0 = 0x10U,
                                 .sts0 = 3U,
                                 .flshcr0 = {8192, 0, 0, 0},
                                 .flshcr1 = 0x63U,
                                 .root_hz = 100000000U,
                                 .lut = {[0] = 0x0a1804ebU,
                                         [1] = 0x26043206U,
                                         [4] = 0x24040405U,
                                         [12] = 0x00000406U,
                                         [20] = 0x08180420U,
                                         [36] = 0x08180402U,
                                         [37] = 0x00002004U}};
    assert(crux_rom_clock_inputs_supported(&p));
    assert(crux_rom_profile_supported(&p));
    uint32_t *fields[] = {&p.ccgr6,      &p.pll_usb1,   &p.pfd480,     &p.mcr0,
                          &p.sts0,       &p.flshcr0[0], &p.flshcr0[1], &p.flshcr0[2],
                          &p.flshcr0[3], &p.flshcr1,    &p.flshcr2,    &p.mcr2};
    uint32_t masks[] = {0x400, 0x80000000, 0x80, 2, 1, 1, 1, 1, 1, 0x400, 1, 0x8000};
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        *fields[i] ^= masks[i];
        assert(!crux_rom_profile_supported(&p));
        *fields[i] ^= masks[i];
    }
    for (unsigned slot = 0; slot < 64; ++slot) {
        if (slot / 4 == 0 || slot / 4 == 1 || slot / 4 == 3 || slot / 4 == 5 || slot / 4 == 9) {
            p.lut[slot] ^= 1;
            assert(!crux_rom_profile_supported(&p));
            p.lut[slot] ^= 1;
        }
    }
    p.root_hz = 0;
    assert(!crux_rom_profile_supported(&p));
    p.root_hz = 104000001;
    assert(!crux_rom_profile_supported(&p));
    p.root_hz = 104000000;
    assert(crux_rom_profile_supported(&p));
    p.cscmr1 = 0;
    assert(!crux_rom_clock_inputs_supported(&p));
    p.cscmr1 = 1U << 29;
    assert(crux_rom_clock_inputs_supported(&p));
    p.cscmr1 = 2U << 29;
    assert(!crux_rom_clock_inputs_supported(&p));
    p.pll_sys = 0x80002001U;
    p.sys_denom = 1;
    p.pfd528 = 0x580000U;
    assert(crux_rom_clock_inputs_supported(&p));
    p.sys_denom = 0;
    assert(!crux_rom_clock_inputs_supported(&p));
    p.sys_denom = 1;
    p.pfd528 = 0;
    assert(!crux_rom_clock_inputs_supported(&p));
    p.cscmr1 = 3U << 29;
    p.pfd480 = 0;
    assert(!crux_rom_clock_inputs_supported(&p));
    return 0;
}
