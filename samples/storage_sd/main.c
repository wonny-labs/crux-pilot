/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_storage.h"
#include <zephyr/kernel.h>

/* Link and measurement image for the RT1060-EVKB microSD composition. The mount runs only when
 * CONFIG_SAMPLE_SD_START is set; the default image touches no card. Do not flash an image with
 * it set until the physical gate has been approved for the current task. */
int main(void)
{
    if (IS_ENABLED(CONFIG_SAMPLE_SD_START)) {
        enum sd_storage_status status = sd_storage_start();
        printk("sd_storage_start: %d\n", (int)status);
    }
    return 0;
}
