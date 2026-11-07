/* SPDX-License-Identifier: Apache-2.0 */
#include "emmc_configuration.h"
#include "configuration_settings.h"

enum emmc_storage_status emmc_configuration_start(void)
{
    enum emmc_storage_status status = emmc_storage_start();

    if (status != EMMC_STORAGE_READY) {
        return status;
    }
    return crux_config_zephyr_init() == CRUX_CONFIG_OK ? EMMC_STORAGE_READY
                                                       : EMMC_STORAGE_CONFIGURATION_FAILED;
}
