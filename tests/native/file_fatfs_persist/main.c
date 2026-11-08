/* SPDX-License-Identifier: Apache-2.0 */
/*
 * One program, several phases selected on the command line, run as separate processes by
 * run_persistence.py. The disk is a zephyr,flash-disk on the flash simulator, which is backed by
 * a host file, so its contents outlive each process.
 *   format  explicitly format the test-owned media, create the File root
 *   write   write <count> files for run <run>
 *   verify  mount (never formatting) and check the files of run <run>
 *   stress  replace files in a loop (<count> updates, 0 = until killed). After each update it
 *           prints "ACK <run> <k>" and, when --crux_idle_ms is set, "IDLE <run> <k>" and rests
 *   check   after a kill: acknowledged files must be intact; the in-flight file may be torn
 */
#include "file_native.h"
#include <crux/file.h>

#include <cmdline.h>
#include <posix_board_if.h>
#include <posix_native_task.h>

#include <ff.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>

#define SLOTS 8
#define MAX_CONTENT 1024

static const char *mode;
static const char *mutant;
static uint32_t run_id = 1, count = SLOTS, idle_ms;

static void add_options(void)
{
    static struct args_struct_t options[] = {
        {.option = "crux_mode",
         .name = "mode",
         .type = 's',
         .dest = (void *)&mode,
         .descript = "phase: format, write, verify, stress or check"},
        {.option = "crux_run",
         .name = "run",
         .type = 'u',
         .dest = (void *)&run_id,
         .descript = "run identifier embedded in the file contents"},
        {.option = "crux_count",
         .name = "count",
         .type = 'u',
         .dest = (void *)&count,
         .descript = "files to write or verify; last acknowledged update for check"},
        {.option = "crux_idle_ms",
         .name = "ms",
         .type = 'u',
         .dest = (void *)&idle_ms,
         .descript = "stress: rest this long after each acknowledged update"},
        {.option = "crux_mutant",
         .name = "mutant",
         .type = 's',
         .dest = (void *)&mutant,
         .descript = "inject a deliberate defect to prove the scenario can fail"},
        ARG_TABLE_ENDMARKER,
    };
    native_add_command_line_opts(options);
}
NATIVE_TASK(add_options, PRE_BOOT_1, 10);

/* The command-line parser leaves an option that was not given as NULL. */
static bool option_is(const char *value, const char *expected)
{
    return value != NULL && strcmp(value, expected) == 0;
}

static FATFS fat;
static struct fs_mount_t mnt = {
    .type = FS_FATFS,
    .mnt_point = "/SD:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static void finish(int code)
{
    fflush(stdout);
    posix_exit(code);
}

static void fail(const char *what, int rc)
{
    printf("FAIL %s rc=%d\n", what, rc);
    finish(1);
}

static size_t content_length(uint32_t update)
{
    return 700 + (update % 5) * 37; /* varying sizes: replacements grow and shrink */
}

static size_t make_content(char *buffer, uint32_t run, uint32_t update)
{
    size_t length = content_length(update);
    int header = snprintf(buffer, length, "run=%u i=%u;", run, update);
    memset(buffer + header, 'a' + (run + update) % 26, length - header);
    return length;
}

static void slot_name(char name[8], uint32_t slot)
{
    snprintf(name, 8, "s%u", slot);
}

/* With the mutant, the update is announced before sync and close (a deliberate defect). */
static crux_file_result_t write_slot(uint32_t slot, uint32_t run, uint32_t update, bool ack_early)
{
    static char data[MAX_CONTENT];
    char name[8];
    crux_file_t *file = NULL;
    size_t transferred = 0;
    size_t length = make_content(data, run, update);

    slot_name(name, slot);
    crux_file_result_t rc = crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file);
    if (rc != CRUX_FILE_OK) {
        return rc;
    }
    rc = crux_file_write(file, data, length, &transferred);
    if (rc == CRUX_FILE_OK && transferred != length) {
        rc = CRUX_FILE_PARTIAL;
    }
    if (ack_early) {
        printf("ACK %u %u\n", run, update);
        printf("IDLE %u %u\n", run, update);
        fflush(stdout);
        k_msleep(idle_ms);
    }
    if (rc == CRUX_FILE_OK) {
        rc = crux_file_sync(file);
    }
    crux_file_result_t closed = crux_file_close(&file);
    return rc != CRUX_FILE_OK ? rc : closed;
}

/* Returns 1 equal, 0 different or unreadable, -1 missing. */
static int slot_matches(uint32_t slot, uint32_t run, uint32_t update)
{
    static char expected[MAX_CONTENT], actual[MAX_CONTENT];
    char name[8];
    crux_file_t *file = NULL;
    size_t transferred = 0;

    slot_name(name, slot);
    crux_file_result_t rc = crux_file_open(name, CRUX_FILE_READ, &file);
    if (rc == CRUX_FILE_ERR_NOT_FOUND) {
        return -1;
    }
    if (rc != CRUX_FILE_OK) {
        return 0;
    }
    size_t length = make_content(expected, run, update);
    rc = crux_file_read(file, actual, sizeof(actual), &transferred);
    (void)crux_file_close(&file);
    return rc == CRUX_FILE_OK && transferred == length && memcmp(expected, actual, length) == 0;
}

static void mount_or_exit(void)
{
    int rc = fs_mount(&mnt);
    if (rc != 0) {
        printf("MOUNT_FAIL %d\n", rc);
        finish(2);
    }
    crux_file_result_t bound = crux_file_zephyr_bind("/SD:/files");
    if (bound != CRUX_FILE_OK) {
        fail("bind", bound);
    }
}

static void do_format(void)
{
    /* Formatting is an explicit, separate operation on test-owned media; no phase formats
     * after a failed mount. */
    if (fs_mount(&mnt) == 0) {
        fs_unmount(&mnt);
    }
    int rc = fs_mkfs(FS_FATFS, (uintptr_t) "SD:", NULL, 0);
    if (rc != 0) {
        fail("mkfs", rc);
    }
    rc = fs_mount(&mnt);
    if (rc != 0) {
        fail("mount after mkfs", rc);
    }
    rc = fs_mkdir("/SD:/files");
    if (rc != 0) {
        fail("mkdir", rc);
    }
    printf("FORMATTED\n");
    finish(0);
}

static void do_write(void)
{
    mount_or_exit();
    for (uint32_t slot = 0; slot < count; slot++) {
        crux_file_result_t rc = write_slot(slot, run_id, slot, false);
        if (rc != CRUX_FILE_OK) {
            fail("write", rc);
        }
    }
    printf("WROTE %u\n", count);
    finish(0);
}

static void do_verify(void)
{
    mount_or_exit();
    for (uint32_t slot = 0; slot < count; slot++) {
        if (slot_matches(slot, run_id, slot) != 1) {
            printf("FAIL verify slot %u\n", slot);
            finish(1);
        }
    }
    printf("VERIFIED %u\n", count);
    finish(0);
}

static void do_stress(void)
{
    bool ack_early = option_is(mutant, "ack_before_durable");

    mount_or_exit();
    for (uint32_t update = 0; count == 0 || update < count; update++) {
        crux_file_result_t rc = write_slot(update % SLOTS, run_id, update, ack_early);
        if (rc != CRUX_FILE_OK) {
            fail("stress write", rc);
        }
        if (!ack_early) {
            printf("ACK %u %u\n", run_id, update);
            if (idle_ms != 0) {
                printf("IDLE %u %u\n", run_id, update);
            }
            fflush(stdout);
            if (idle_ms != 0) {
                k_msleep(idle_ms);
            }
        }
    }
    finish(0);
}

static void do_check(void)
{
    uint32_t last = count; /* last acknowledged update */
    uint32_t inflight = (last + 1) % SLOTS;
    const char *state = "none";

    mount_or_exit();
    for (uint32_t slot = 0; slot < SLOTS; slot++) {
        bool written = slot <= last;
        uint32_t newest = written ? slot + SLOTS * ((last - slot) / SLOTS) : 0;
        if (slot != inflight) {
            int match = written ? slot_matches(slot, run_id, newest) : -1;
            if (written && match != 1) {
                printf("FAIL acknowledged update %u in slot %u is not intact (%d)\n", newest, slot,
                       match);
                finish(1);
            }
            continue;
        }
        /* The slot being replaced when the process died may legitimately be torn. */
        if (written && slot_matches(slot, run_id, newest) == 1) {
            state = "old";
        } else if (slot_matches(slot, run_id, last + 1) == 1) {
            state = "new";
        } else if (slot_matches(slot, run_id, last + 1) == -1) {
            state = "missing";
        } else {
            state = "torn";
        }
    }
    printf("CHECK_OK inflight=%s\n", state);
    finish(0);
}

int main(void)
{
    if (option_is(mode, "format")) {
        do_format();
    } else if (option_is(mode, "write")) {
        do_write();
    } else if (option_is(mode, "verify")) {
        do_verify();
    } else if (option_is(mode, "stress")) {
        do_stress();
    } else if (option_is(mode, "check")) {
        do_check();
    }
    printf("FAIL unknown mode '%s'\n", mode ? mode : "(none)");
    finish(64);
    return 0;
}
