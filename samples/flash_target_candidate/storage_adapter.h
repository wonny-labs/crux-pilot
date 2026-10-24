/* SPDX-License-Identifier: Apache-2.0 */
/* Sample-private, caller-serialized maintenance ownership. No target admission. */
#ifndef CRUX_CANDIDATE_STORAGE_ADAPTER_H
#define CRUX_CANDIDATE_STORAGE_ADAPTER_H
#include "rom_command.h"
/* The target leaf always refuses. Tests substitute qualified command leaves. */
int crux_candidate_command_prerequisites(void);
int crux_candidate_storage_begin(void);
bool crux_candidate_storage_ready(void);
bool crux_candidate_storage_finish(void);
int crux_candidate_read(enum crux_pilot_flash_owner owner, int64_t offset, void *bytes, size_t len);
int crux_candidate_write(enum crux_pilot_flash_owner owner, int64_t offset, const void *bytes,
                         size_t len);
int crux_candidate_erase(enum crux_pilot_flash_owner owner, int64_t offset, size_t len);
#endif
