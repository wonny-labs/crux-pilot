/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "emmc_storage.h"

/* The mount sequence of emmc_storage.h, then Configuration on the Settings File backend. Project
 * code, not a Crux API.
 *
 * The Settings backend creates the directories of its file path when it initialises. This
 * composition's rule is that the firmware creates no directory, so settings are initialised only
 * after emmc_storage_start returned EMMC_STORAGE_READY, which means that /MMC:/files and
 * /MMC:/settings exist. On any other status nothing is initialised and the medium is not written.
 *
 * Returns what emmc_storage_start returned, or EMMC_STORAGE_CONFIGURATION_FAILED when the storage
 * is ready and crux_config_zephyr_init failed. The volume then stays mounted and the File root
 * stays bound, so a second call reports EMMC_STORAGE_ALREADY_STARTED and does not retry the
 * settings: call crux_config_zephyr_init again for that. */
enum emmc_storage_status emmc_configuration_start(void);
