/* SPDX-License-Identifier: Apache-2.0 */
#include "rom_command.h"
/* Separate RAM metadata: native normal-init configuration remains refused. */
static struct memc_flexspi_config crux_rom_memc_config = {
    .reg_base = {.addr = 0x402a8000},
    .ahb = {.addr = 0x60000000},
};
static struct memc_flexspi_data crux_rom_memc_data;
int __attribute__((noinline)) crux_rom_memc_bind(struct device *copy) {
    /* This exact candidate has direct MMIO. No mapping/configuration write. */
    BUILD_ASSERT(!IS_ENABLED(CONFIG_MMU));
    crux_rom_memc_data.size[kFLEXSPI_PortA1] = 8192U * 1024U;
    crux_rom_memc_data.port_luts[kFLEXSPI_PortA1].lut_offset = 0;
    crux_rom_memc_data.port_luts[kFLEXSPI_PortA1].lut_used = 40;
    copy->config = &crux_rom_memc_config;
    copy->data = &crux_rom_memc_data;
    /* Exact consumed fields: get_base uses reg_base; A1 transfer consumes its
     * LUT offset, no preceding size entries. Validate remaining geometry too. */
    if ((uintptr_t)get_base(copy) != 0x402a8000 || crux_rom_memc_data.size[0] != 8388608U ||
        crux_rom_memc_data.port_luts[0].lut_offset != 0 ||
        crux_rom_memc_data.port_luts[0].lut_used != 40) {
        return -EACCES;
    }
    for (unsigned i = 1; i < kFLEXSPI_PortCount; ++i) {
        if (crux_rom_memc_data.size[i] || crux_rom_memc_data.port_luts[i].lut_offset ||
            crux_rom_memc_data.port_luts[i].lut_used) {
            return -EACCES;
        }
    }
    return 0;
}
