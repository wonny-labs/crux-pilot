/* SPDX-License-Identifier: Apache-2.0 */
/* Included only in the pinned isolated native NOR translation unit. */
#include "rom_command.h"
_Static_assert(JESD216_READ_ID_LEN == 3, "identity helper length changed");
_Static_assert(SPI_NOR_WIP_BIT == 1U && SPI_NOR_WEL_BIT == 2U, "status definitions changed");
static struct flash_flexspi_nor_data crux_rom_nor;
static uint8_t crux_rom_identity[3];
static struct {
    enum crux_rom_operation operation;
    enum crux_pilot_flash_owner owner;
    int64_t offset;
    size_t length;
    uint32_t bounce[64];
} crux_rom_command;

int __attribute__((noinline)) crux_rom_command_prepare(const struct crux_rom_request *r) {
    crux_rom_command.operation = CRUX_ROM_NONE;
    if (!r || r->operation < CRUX_ROM_PROGRAM || r->operation > CRUX_ROM_STATUS ||
        !crux_pilot_flash_owner_request_valid(r->owner, r->offset, r->length,
                                              r->operation == CRUX_ROM_SECTOR)) {
        return -EACCES;
    }
    if ((r->operation == CRUX_ROM_SECTOR && r->length != 4096) ||
        (r->operation != CRUX_ROM_SECTOR && r->length > 256) ||
        (r->operation == CRUX_ROM_PROGRAM &&
         (!r->payload || (uint64_t)r->offset % 256 + r->length > 256)) ||
        (r->operation == CRUX_ROM_STATUS && r->length != 1)) {
        return -EACCES;
    }
    if (crux_rom_memc_bind(&crux_rom_nor.controller)) {
        return -EACCES;
    }
    crux_rom_nor.port = kFLEXSPI_PortA1;
    crux_rom_nor.legacy_poll = true;
    crux_rom_command.owner = r->owner;
    crux_rom_command.offset = r->offset;
    crux_rom_command.length = r->length;
    if (r->operation == CRUX_ROM_PROGRAM) {
        volatile uint8_t *dst = (volatile uint8_t *)crux_rom_command.bounce;
        const volatile uint8_t *src = r->payload;
        for (size_t i = 0; i < r->length; ++i) {
            dst[i] = src[i];
        }
    }
    crux_rom_command.operation = r->operation;
    return 0;
}

static void crux_rom_check_deadline(void) {
    if (crux_rom_elapsed_expired()) {
        crux_flash_poc_terminal(-ETIMEDOUT);
    }
}
static void crux_rom_check_result(int result) {
    if (result) {
        crux_flash_poc_terminal(result);
    }
}
static void crux_rom_ready_idle(void) {
    uint32_t status;
    do {
        crux_rom_check_deadline();
        crux_rom_identity_check(&crux_rom_nor.controller, crux_rom_slot_installed());
        status = 0;
        crux_rom_check_result(crux_rom_native_read_status(&crux_rom_nor, &status));
    } while (status & 1U);
    crux_rom_check_deadline();
    memc_flexspi_wait_bus_idle(&crux_rom_nor.controller);
    crux_rom_check_deadline();
}
int __attribute__((noinline)) crux_rom_command_dispatch(void) {
    enum crux_rom_operation operation = crux_rom_command.operation;
    if (operation == CRUX_ROM_NONE) {
        return 0; /* Existing unchanged read-only island observation. */
    }
    /* Defense in depth for private staged RAM state, before the first command. */
    if (operation < CRUX_ROM_PROGRAM || operation > CRUX_ROM_STATUS ||
        !crux_pilot_flash_owner_request_valid(crux_rom_command.owner, crux_rom_command.offset,
                                              crux_rom_command.length,
                                              operation == CRUX_ROM_SECTOR) ||
        (operation == CRUX_ROM_SECTOR && crux_rom_command.length != 4096) ||
        (operation != CRUX_ROM_SECTOR && crux_rom_command.length > 256) ||
        (operation == CRUX_ROM_PROGRAM &&
         (uint64_t)crux_rom_command.offset % 256 + crux_rom_command.length > 256) ||
        (operation == CRUX_ROM_STATUS && crux_rom_command.length != 1)) {
        crux_flash_poc_terminal(-EACCES);
    }
    crux_rom_check_deadline();
    /* Full inherited table/port/idle proof precedes even a status probe. */
    crux_rom_identity_check(&crux_rom_nor.controller, false);
    if (operation != CRUX_ROM_STATUS) {
        crux_rom_ready_idle();
    }
    if (operation == CRUX_ROM_PROGRAM || operation == CRUX_ROM_SECTOR) {
        crux_rom_identity_install(&crux_rom_nor.controller);
        crux_rom_check_result(crux_rom_native_read_id_helper(&crux_rom_nor, crux_rom_identity));
        crux_rom_check_deadline();
        crux_rom_identity_check(&crux_rom_nor.controller, true);
        /* Restore only after a successful positive status and controller idle. */
        crux_rom_ready_idle();
        crux_rom_identity_restore(&crux_rom_nor.controller);
        if (crux_rom_identity[0] != 0x9d || crux_rom_identity[1] != 0x70 ||
            crux_rom_identity[2] != 0x17) {
            crux_flash_poc_terminal(-ENODEV);
        }
    }
    if (operation == CRUX_ROM_PROGRAM || operation == CRUX_ROM_SECTOR) {
        crux_rom_check_result(crux_rom_native_write_enable(&crux_rom_nor));
        crux_rom_check_deadline();
        crux_rom_identity_check(&crux_rom_nor.controller, false);
        uint32_t status = 0;
        crux_rom_check_result(crux_rom_native_read_status(&crux_rom_nor, &status));
        crux_rom_check_deadline();
        if ((status & (SPI_NOR_WIP_BIT | SPI_NOR_WEL_BIT)) != SPI_NOR_WEL_BIT) {
            crux_flash_poc_terminal(-EACCES);
        }
        crux_rom_identity_check(&crux_rom_nor.controller, false);
        if (operation == CRUX_ROM_PROGRAM) {
            crux_rom_check_result(
                crux_rom_native_page_program(&crux_rom_nor, crux_rom_command.offset,
                                             crux_rom_command.bounce, crux_rom_command.length));
        } else {
            crux_rom_check_result(
                crux_rom_native_erase_sector(&crux_rom_nor, crux_rom_command.offset));
        }
    } else if (operation == CRUX_ROM_READ) {
        flexspi_transfer_t transfer = {
            .deviceAddress = crux_rom_command.offset,
            .port = kFLEXSPI_PortA1,
            .cmdType = kFLEXSPI_Read,
            .SeqNumber = 1,
            .seqIndex = 0,
            .data = crux_rom_command.bounce,
            .dataSize = crux_rom_command.length,
        };
        crux_rom_check_result(memc_flexspi_transfer(&crux_rom_nor.controller, &transfer));
    }
    /* A controller-idle observation never substitutes for NOR WIP=0. */
    crux_rom_ready_idle();
    if (operation == CRUX_ROM_PROGRAM || operation == CRUX_ROM_SECTOR) {
        /* Native controller buffer/FIFO refresh only after positive NOR ready
         * and bus idle. This issues no NOR reset or system reset. */
        crux_rom_check_result(memc_flexspi_reset(&crux_rom_nor.controller));
        crux_rom_check_deadline();
        memc_flexspi_wait_bus_idle(&crux_rom_nor.controller);
        crux_rom_check_deadline();
    }
    crux_rom_command.operation = CRUX_ROM_NONE;
    return 0;
}
