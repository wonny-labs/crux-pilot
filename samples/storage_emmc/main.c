/* SPDX-License-Identifier: Apache-2.0 */
#include "emmc_storage.h"
#ifdef CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE
#include "emmc_configuration.h"
#endif
#include <zephyr/kernel.h>

/* Reference composition of the File component on Reliance Edge over an eMMC disk. The mount runs
 * only when CONFIG_SAMPLE_EMMC_START is set; the default image touches no medium. With
 * CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE the sequence also initialises Configuration's settings,
 * after the layout check. Do not flash an image with it set until the physical gate has been
 * approved for the current task. */
int main(void)
{
    if (IS_ENABLED(CONFIG_SAMPLE_EMMC_START)) {
#ifdef CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE
        enum emmc_storage_status status = emmc_configuration_start();
        printk("emmc_configuration_start: %d\n", (int)status);
#else
        enum emmc_storage_status status = emmc_storage_start();
        printk("emmc_storage_start: %d\n", (int)status);
#endif
    }
    return 0;
}
