/* SPDX-License-Identifier: Apache-2.0 */
#include "emmc_storage.h"
#include <zephyr/kernel.h>

/* Reference composition of the File component on Reliance Edge over an eMMC disk. The mount runs
 * only when CONFIG_SAMPLE_EMMC_START is set; the default image touches no medium. Do not flash an
 * image with it set until the physical gate has been approved for the current task. */
int main(void)
{
    if (IS_ENABLED(CONFIG_SAMPLE_EMMC_START)) {
        enum emmc_storage_status status = emmc_storage_start();
        printk("emmc_storage_start: %d\n", (int)status);
    }
    return 0;
}
