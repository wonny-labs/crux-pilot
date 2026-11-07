/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* Maps the names that the microSD test sources of samples/storage_sd_interrupt and
 * samples/storage_sd_replace use onto the Reliance Edge mount helper of samples/storage_emmc, so
 * that those sources compile unchanged. This directory comes first in the include path, ahead of
 * samples/storage_sd, whose header of the same name this one stands in for. The constants of the
 * two enums have the same values in the same order. The mount point stays /MMC: whatever the disk
 * is called; only the disk name (CONFIG_SAMPLE_EMMC_DISK_NAME, "SD" here) differs. */
#include "emmc_storage.h"

#define SD_STORAGE_SETTINGS_DIR EMMC_STORAGE_SETTINGS_DIR
#define SD_STORAGE_READY EMMC_STORAGE_READY
#define sd_storage_status emmc_storage_status
#define sd_storage_start emmc_storage_start
