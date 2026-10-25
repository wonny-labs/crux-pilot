/* SPDX-License-Identifier: Apache-2.0 */
/* Included after only the actual pinned status/JEDEC read helpers. */
#include "flash_poc.h"
#include "observation.h"
#include "rom_command.h"
#include "rom_deadline.h"
_Static_assert(JESD216_READ_ID_LEN == 3, "identity helper length changed");
_Static_assert(SPI_NOR_WIP_BIT == 1U && SPI_NOR_WEL_BIT == 2U, "status definitions changed");
static struct flash_flexspi_nor_data crux_observation_nor;
static bool crux_observation_prepared, crux_observation_prepare_claimed;
int crux_observation_prepare(void) {
    if (crux_observation_prepare_claimed) {
        return -EALREADY;
    }
    crux_observation_prepare_claimed = true;
    if (crux_rom_memc_bind(&crux_observation_nor.controller)) {
        return -EACCES;
    }
    crux_observation_nor.port = kFLEXSPI_PortA1;
    crux_observation_nor.legacy_poll = true;
    crux_observation_prepared = true;
    return 0;
}
static void crux_observation_check(int result) {
    if (result) {
        crux_flash_poc_terminal(result);
    }
}
int crux_rom_command_dispatch(void) {
    if (!crux_observation_prepared) {
        crux_flash_poc_terminal(-EACCES);
    }
    crux_observation_prepared = false;
    crux_rom_identity_check(&crux_observation_nor.controller, false);
    uint32_t status = 0;
    crux_observation_check(crux_rom_native_read_status(&crux_observation_nor, &status));
    crux_observation_result.status_initial = status;
    crux_observation_result.flags |= CRUX_OBSERVATION_INITIAL_STATUS_VALID;
    crux_rom_candidate_wait_check();
    /* Fixed initial observation. Never poll an initially busy/unsupported NOR
     * until it happens to look acceptable, or return to XIP on that assumption. */
    if (status & SPI_NOR_WIP_BIT) {
        crux_flash_poc_terminal(-EBUSY);
    }
    memc_flexspi_wait_bus_idle(&crux_observation_nor.controller);
    if (status & SPI_NOR_WEL_BIT) {
        crux_rom_identity_check(&crux_observation_nor.controller, false);
        crux_observation_result.flags |= CRUX_OBSERVATION_KNOWN_READY;
        crux_rom_candidate_deadline_finish();
        crux_observation_result.result = -EACCES;
        crux_observation_result.phase = CRUX_OBSERVATION_REFUSED;
        return 0; /* Positive WIP=0; no slot or NOR change. */
    }
    crux_rom_identity_install(&crux_observation_nor.controller);
    crux_observation_result.flags |= CRUX_OBSERVATION_SLOT_INSTALLED;
    uint8_t id[3] = {0};
    crux_observation_check(crux_rom_native_read_id_helper(&crux_observation_nor, id));
    for (unsigned i = 0; i < 3; ++i) {
        crux_observation_result.id[i] = id[i];
    }
    crux_observation_result.flags |= CRUX_OBSERVATION_ID_VALID;
    if (id[0] == 0x9d && id[1] == 0x70 && id[2] == 0x17) {
        crux_observation_result.flags |= CRUX_OBSERVATION_ID_MATCH;
    }
    crux_rom_identity_check(&crux_observation_nor.controller, true);
    status = 0;
    crux_observation_check(crux_rom_native_read_status(&crux_observation_nor, &status));
    crux_observation_result.status_final = status;
    crux_observation_result.flags |= CRUX_OBSERVATION_FINAL_STATUS_VALID;
    crux_rom_candidate_wait_check();
    if (status & (SPI_NOR_WIP_BIT | SPI_NOR_WEL_BIT)) {
        crux_flash_poc_terminal(-EBUSY);
    }
    memc_flexspi_wait_bus_idle(&crux_observation_nor.controller);
    crux_observation_result.flags |= CRUX_OBSERVATION_KNOWN_READY;
    crux_rom_identity_restore(&crux_observation_nor.controller);
    crux_observation_result.flags |= CRUX_OBSERVATION_SLOT_RESTORED;
    crux_rom_candidate_deadline_finish();
    /* Assembly restores caller vectors/stacks/CONTROL/PRIMASK after return;
     * the outer owner alone may publish COMPLETE after that restoration. */
    crux_observation_result.phase = CRUX_OBSERVATION_RESTORED;
    return 0;
}
