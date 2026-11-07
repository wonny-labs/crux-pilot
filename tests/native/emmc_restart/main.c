/* SPDX-License-Identifier: Apache-2.0 */
/*
 * One program, two phases selected on the command line, run as separate processes by
 * run_restart.py. The persistent state of the eMMC simulator is backed by a host file, so it
 * outlives each process.
 *   write   write a pattern; --restart-sync and --restart-flush then ask for it to be kept
 *   verify  read through the MMC stack and compare with the pattern
 */
#include "emmc_sim.h"

#include <cmdline.h>
#include <posix_board_if.h>
#include <posix_native_task.h>

#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/storage/disk_access.h>

#define DISK "MMC"
#define SECTOR_SIZE 512U
#define FIRST_SECTOR 100U
#define SECTORS 8U

#define EXT_CSD_FLUSH_CACHE 32U
#define MMC_SWITCH_WRITE_BYTE 3U

static const char *phase;
static uint32_t seed;
static bool sync_after_write;
static bool flush_after_write;

static uint8_t pattern[SECTORS * SECTOR_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);
static uint8_t contents[SECTORS * SECTOR_SIZE] __aligned(CONFIG_SDHC_BUFFER_ALIGNMENT);

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "phase",
         .name = "phase",
         .type = 's',
         .dest = (void *)&phase,
         .descript = "phase: write or verify"},
        {.option = "restart-seed",
         .name = "n",
         .type = 'u',
         .dest = (void *)&seed,
         .descript = "seed of the pattern; also the id of the trace mark after a write"},
        {.option = "restart-sync",
         .is_switch = true,
         .type = 'b',
         .dest = (void *)&sync_after_write,
         .descript = "write: send DISK_IOCTL_CTRL_SYNC afterwards"},
        {.option = "restart-flush",
         .is_switch = true,
         .type = 'b',
         .dest = (void *)&flush_after_write,
         .descript = "write: send FLUSH_CACHE to the host driver afterwards"},
        ARG_TABLE_ENDMARKER,
    };

    native_add_command_line_opts(options);
}
NATIVE_TASK(add_options, PRE_BOOT_1, 10);

static void finish(const char *what, int code)
{
    printf("%s\n", what);
    fflush(stdout);
    posix_exit(code);
}

/* The stack sends no flush at the pin, so this sends the command itself to reach the cache. */
static int send_flush(void)
{
    struct sdhc_command cmd = {
        .opcode = SD_SWITCH,
        .arg = (MMC_SWITCH_WRITE_BYTE << 24) | (EXT_CSD_FLUSH_CACHE << 16) | (1U << 8),
        .response_type = SD_RSP_TYPE_R1b,
    };

    return sdhc_request(DEVICE_DT_GET(DT_NODELABEL(emmc_sim0)), &cmd, NULL);
}

static void do_write(void)
{
    if (disk_access_write(DISK, pattern, FIRST_SECTOR, SECTORS) != 0) {
        finish("FAIL write", 1);
    }
    emmc_sim_trace_mark(seed);
    if (sync_after_write && disk_access_ioctl(DISK, DISK_IOCTL_CTRL_SYNC, NULL) != 0) {
        finish("FAIL sync", 1);
    }
    if (flush_after_write && send_flush() != 0) {
        finish("FAIL flush", 1);
    }
    finish("WROTE", 0);
}

static void do_verify(void)
{
    if (disk_access_read(DISK, contents, FIRST_SECTOR, SECTORS) != 0) {
        finish("FAIL read", 1);
    }
    if (memcmp(contents, pattern, sizeof(pattern)) != 0) {
        finish("MISMATCH", 3);
    }
    finish("VERIFIED", 0);
}

int main(void)
{
    for (size_t i = 0; i < sizeof(pattern); i++) {
        pattern[i] = (uint8_t)(seed + i + i / SECTOR_SIZE);
    }
    if (disk_access_init(DISK) != 0) {
        finish("FAIL init", 2);
    }
    if (phase != NULL && strcmp(phase, "write") == 0) {
        do_write();
    } else if (phase != NULL && strcmp(phase, "verify") == 0) {
        do_verify();
    }
    finish("FAIL unknown phase", 64);
    return 0;
}
