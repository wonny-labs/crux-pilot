/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_file_check.h"
#include "sd_storage.h"
#include <zephyr/kernel.h>

/* Link and measurement image for the RT1060-EVKB microSD composition. The mount runs only when
 * CONFIG_SAMPLE_SD_START is set; the default image touches no card. CONFIG_SAMPLE_SD_FILE_CHECK
 * additionally writes to the card. Do not flash an image with either set until the physical gate
 * has been approved for the current task. */
int main(void)
{
    if (IS_ENABLED(CONFIG_SAMPLE_SD_START)) {
        enum sd_storage_status status = sd_storage_start();
        printk("sd_storage_start: %d\n", (int)status);
        if (IS_ENABLED(CONFIG_SAMPLE_SD_FILE_CHECK) && status == SD_STORAGE_READY) {
            struct sd_file_check check;
            enum sd_file_check_status result = sd_file_check_run(&check);
            printk("sd_file_check: status=%d boot=%u pattern_created=%d\n", (int)result,
                   (unsigned)check.boot_count, (int)check.pattern_created);
        }
    }
    return 0;
}
