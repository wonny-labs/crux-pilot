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
 * BOOT and RECOVER repeat every REPORT_EVERY records: the console shares the board's power USB, so
 * after a power cut the serial port returns seconds after the board has already booted.
 */
enum { REPORT_EVERY = 16 };

static void report(const struct sd_interrupt_recovery *recovery)
{
    printk("BOOT %u\n", (unsigned)recovery->boot);
    if (recovery->boot > 1u) {
        printk("RECOVER run=%u present=%d last_valid=%u torn=%u damaged=%d\n",
               (unsigned)(recovery->boot - 1u), (int)recovery->previous_run_present,
               (unsigned)recovery->last_valid, (unsigned)recovery->torn_bytes,
               (int)recovery->damaged_record);
    }
}

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
    for (uint32_t seq = 1; seq != 0; seq++) {
        if ((seq - 1u) % REPORT_EVERY == 0u) {
            report(&recovery);
        }
        if (sd_interrupt_append(run, seq) != SD_INTERRUPT_OK) {
            printk("append_failed: %u\n", (unsigned)seq);
            break;
        }
        printk("ACK %u\n", (unsigned)seq);
    }
    return 0;
}
