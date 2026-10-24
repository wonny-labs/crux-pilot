#include <assert.h>
#include <string.h>
int main(void) {
    ccm.CSCMR1 = 3U << 29;
    ccm.CCGR6 = 3U << 10;
    analog.PLL_USB1 = 0x80003000U;
    analog.PFD_480 = 0x58U;
    flexspi.MCR0 = 0x10;
    flexspi.STS0 = 3;
    flexspi.FLSHCR0[0] = 8192;
    flexspi.FLSHCR1[0] = 0x63;
    flexspi.LUT[0] = 0x0a1804eb;
    flexspi.LUT[1] = 0x26043206;
    flexspi.LUT[4] = 0x24040405;
    flexspi.LUT[12] = 0x406;
    flexspi.LUT[20] = 0x08180420;
    flexspi.LUT[36] = 0x08180402;
    flexspi.LUT[37] = 0x2004;
    unsigned char before[sizeof(flexspi)];
    memcpy(before, &flexspi, sizeof(flexspi));
    assert(crux_rom_read_confirm_supported());
    assert(getters == 1);
    assert(!memcmp(before, &flexspi, sizeof(flexspi)));
    analog.PFD_480 = 0;
    assert(!crux_rom_read_confirm_supported());
    assert(getters == 1);
    analog.PFD_480 = 0x58;
    rate = 120000000;
    assert(!crux_rom_read_confirm_supported());
    assert(getters == 2);
    rate = 100000000;
    flexspi.LUT[0] ^= 1;
    assert(!crux_rom_read_confirm_supported());
    flexspi.LUT[0] ^= 1;
    change_clock = 1;
    assert(!crux_rom_read_confirm_supported());
    change_clock = 0;
    osc = 0;
    unsigned prior = getters;
    assert(!crux_rom_read_confirm_supported());
    assert(getters == prior);
    return 0;
}
