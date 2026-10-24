/* SPDX-License-Identifier: Apache-2.0 */
/* Private isolated software candidate. No production command admission. */
#ifndef CRUX_ROM_COMMAND_H
#define CRUX_ROM_COMMAND_H
#include "request_fence.h"
#include <errno.h>
struct device;
enum crux_rom_operation {
    CRUX_ROM_NONE,
    CRUX_ROM_PROGRAM,
    CRUX_ROM_SECTOR,
    CRUX_ROM_READ,
    CRUX_ROM_STATUS
};
struct crux_rom_request {
    enum crux_rom_operation operation;
    enum crux_pilot_flash_owner owner;
    int64_t offset;
    size_t length;
    const void *payload;
};
/* Preparation copies input before busy context; output remains in private RAM. */
int crux_rom_command_prepare(const struct crux_rom_request *request);
int crux_rom_command_dispatch(void);
/* Copy only a completed READ result, exact length, once, after known-ready return. */
int crux_rom_command_read_result(void *bytes, size_t length);
int crux_rom_command_submit(const struct crux_rom_request *request);
int crux_rom_memc_bind(struct device *copy);
bool crux_rom_slot_installed(void);
void crux_rom_identity_check(const struct device *controller, bool installed);
void crux_rom_identity_install(const struct device *controller);
void crux_rom_identity_restore(const struct device *controller);
bool crux_rom_elapsed_expired(void);
#endif
