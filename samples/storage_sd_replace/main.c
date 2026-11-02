/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_interrupt.h"
#include "sd_replace.h"
#include "sd_storage.h"
#include <stdio.h>
#include <zephyr/kernel.h>

/* Replace-pattern power-cut test image for the RT1060-EVKB microSD card. Default builds touch no
 * card. With CONFIG_SAMPLE_SD_REPLACE_TEST it rewrites the settings file through a temporary and
 * fs_rename until power is cut; do not flash it until the physical gate has been approved for the
 * current task.
 *
 * Console lines at 115200 baud; BOOT, CLASS and GEN end with a checksum " *XX":
 *   sd_storage_start: <status>
 *   BOOT <n> *XX
 *   CLASS run=<n-1> cfg=<none|valid|bad> cfg_gen=<g> cmp=<none|valid|bad> cmp_gen=<g> *XX
 *   GEN <g> *XX      printed after the rename of generation g returned
 *
 * BOOT and CLASS repeat every REPORT_EVERY generations: the console shares the board's power USB,
 * so after a cut the serial port returns seconds after the board has booted.
 */
enum { REPORT_EVERY = 16 };

static const char *state_name(enum sd_replace_state state)
{
    return state == SD_REPLACE_NONE ? "none" : state == SD_REPLACE_VALID ? "valid" : "bad";
}

static void emit(const char *text)
{
    printk("%s *%02X\n", text, (unsigned)sd_interrupt_checksum(text));
}

static void report(const struct sd_replace_recovery *recovery)
{
    char line[112];

    snprintf(line, sizeof(line), "BOOT %u", (unsigned)recovery->boot);
    emit(line);
    if (recovery->boot > 1u) {
        snprintf(line, sizeof(line), "CLASS run=%u cfg=%s cfg_gen=%u cmp=%s cmp_gen=%u",
                 (unsigned)(recovery->boot - 1u), state_name(recovery->cfg.state),
                 (unsigned)recovery->cfg.gen, state_name(recovery->cmp.state),
                 (unsigned)recovery->cmp.gen);
        emit(line);
    }
}

int main(void)
{
    if (!IS_ENABLED(CONFIG_SAMPLE_SD_REPLACE_TEST)) {
        return 0;
    }
    enum sd_storage_status status = sd_storage_start();
    printk("sd_storage_start: %d\n", (int)status);
    if (status != SD_STORAGE_READY) {
        return 0;
    }
    struct sd_replace_recovery recovery;
    enum sd_replace_status result = sd_replace_begin(&recovery);
    if (result != SD_REPLACE_OK) {
        printk("sd_replace_begin: %d\n", (int)result);
        return 0;
    }
    /* Continue after the highest generation either file shows, so generations stay monotonic. */
    uint32_t first =
        (recovery.cfg.gen > recovery.cmp.gen ? recovery.cfg.gen : recovery.cmp.gen) + 1u;

    for (uint32_t gen = first; gen != 0; gen++) {
        if ((gen - first) % REPORT_EVERY == 0u) {
            report(&recovery);
        }
        if (sd_replace_generation(gen) != SD_REPLACE_OK) {
            printk("replace_failed: %u\n", (unsigned)gen);
            break;
        }
        char line[24];

        snprintf(line, sizeof(line), "GEN %u", (unsigned)gen);
        emit(line);
    }
    return 0;
}
