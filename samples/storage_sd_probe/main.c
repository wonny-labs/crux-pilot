/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/kernel.h>

#include "sd_file_check.h"
#include "sd_probe.h"
#include "sd_storage.h"
#include <stdio.h>
#include <zephyr/debug/thread_analyzer.h>

/* Physical-gate probes for the RT1060-EVKB microSD card. Default builds touch no card. Both probes
 * write to the card: do not flash an image with either set until the physical gate has been
 * approved for the current task. Console at 115200 baud.
 *
 * CONFIG_SAMPLE_SD_STACK_PROBE: mount, run the file check twice and the FatFs workload twice, then
 * print the thread analyzer report. The main thread's "unused" line is the stack margin; interrupt
 * stacks are not covered by it.
 *
 * CONFIG_SAMPLE_SD_REMOVAL_PROBE: write "probe" every 200 ms and print "OP <n> state=<s>
 * failures=<f> recoveries=<r> open/write/sync/close=<codes> remount=<rc>". Remove the card, wait,
 * put it back: after the first failure the volume is unmounted and a remount is retried every
 * second.
 */
enum { STEP_PERIOD_MS = 200, LOST_PERIOD_MS = 1000, PRINT_EVERY = 25 };

static void stack_probe(void)
{
    struct sd_file_check check;

    for (int round = 1; round <= 2; round++) {
        int work = sd_probe_workload();
        enum sd_file_check_status result = sd_file_check_run(&check);

        printk("stack_probe round %d: workload=%d file_check=%d boot=%u\n", round, work,
               (int)result, (unsigned)check.boot_count);
    }
    thread_analyzer_print(0);
}

static void removal_probe(void)
{
    struct sd_probe probe;
    enum sd_probe_state last = SD_PROBE_RECOVERED; /* forces the first line */

    sd_probe_init(&probe);
    for (;;) {
        sd_probe_step(&probe);
        if (probe.state != last || probe.ops % PRINT_EVERY == 0u || probe.state == SD_PROBE_LOST) {
            printk("OP %u state=%d failures=%u recoveries=%u codes=%d,%d,%d,%d remount=%d\n",
                   (unsigned)probe.ops, (int)probe.state, (unsigned)probe.failures,
                   (unsigned)probe.recoveries, probe.codes[0], probe.codes[1], probe.codes[2],
                   probe.codes[3], probe.remount);
        }
        last = probe.state;
        k_msleep(probe.state == SD_PROBE_LOST ? LOST_PERIOD_MS : STEP_PERIOD_MS);
    }
}

int main(void)
{
    if (!IS_ENABLED(CONFIG_SAMPLE_SD_STACK_PROBE) && !IS_ENABLED(CONFIG_SAMPLE_SD_REMOVAL_PROBE)) {
        return 0;
    }
    enum sd_storage_status status = sd_storage_start();

    printk("sd_storage_start: %d\n", (int)status);
    if (status != SD_STORAGE_READY) {
        return 0;
    }
    if (IS_ENABLED(CONFIG_SAMPLE_SD_STACK_PROBE)) {
        stack_probe();
    } else {
        removal_probe();
    }
    return 0;
}
