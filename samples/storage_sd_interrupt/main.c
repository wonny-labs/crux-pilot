/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_interrupt.h"
#include "sd_storage.h"
#include <zephyr/kernel.h>

/* Interrupted-write test image for the RT1060-EVKB microSD card. Default builds touch no card. With
 * CONFIG_SAMPLE_SD_INTERRUPT_TEST it writes to the card until power is cut; do not flash it until
 * the physical gate has been approved for the current task. Console lines (115200 baud):
 *   sd_storage_start: <status>
 *   BOOT <n>
 *   RECOVER run=<n-1> present=<0|1> last_valid=<k> torn=<bytes> damaged=<0|1>
 *   ACK <seq>            one per record, printed only after the record was synced
 */
int main(void)
{
    if (!IS_ENABLED(CONFIG_SAMPLE_SD_INTERRUPT_TEST)) {
        return 0;
    }
    enum sd_storage_status status = sd_storage_start();
    printk("sd_storage_start: %d\n", (int)status);
    if (status != SD_STORAGE_READY) {
        return 0;
    }
    struct sd_interrupt_recovery recovery;
    crux_file_t *run = NULL;
    enum sd_interrupt_status result = sd_interrupt_begin(&recovery, &run);
    if (result != SD_INTERRUPT_OK) {
        printk("sd_interrupt_begin: %d\n", (int)result);
        return 0;
    }
    printk("BOOT %u\n", (unsigned)recovery.boot);
    if (recovery.boot > 1u) {
        printk("RECOVER run=%u present=%d last_valid=%u torn=%u damaged=%d\n",
               (unsigned)(recovery.boot - 1u), (int)recovery.previous_run_present,
               (unsigned)recovery.last_valid, (unsigned)recovery.torn_bytes,
               (int)recovery.damaged_record);
    }
    for (uint32_t seq = 1; seq != 0; seq++) {
        if (sd_interrupt_append(run, seq) != SD_INTERRUPT_OK) {
            printk("append_failed: %u\n", (unsigned)seq);
            break;
        }
        printk("ACK %u\n", (unsigned)seq);
    }
    return 0;
}
