/* Record format and recovery logic of the interrupted-write test (samples/storage_sd_interrupt) on
 * a RAM disk named like the EVKB card. The previous boot's handle is closed by the test to stand in
 * for a process that no longer exists, so nothing here is power-loss evidence. Tests run in name
 * order: the File provider binds once per process. */
#include "sd_interrupt.h"
#include "sd_storage.h"
#include <crux/file.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

static crux_file_t *run;

static void provision(void)
{
    static FATFS fat;
    struct fs_mount_t mount = {
        .type = FS_FATFS,
        .mnt_point = SD_STORAGE_MOUNT,
        .fs_data = &fat,
        .flags = FS_MOUNT_FLAG_NO_FORMAT,
    };

    /* Test-owned simulator media only: the host-side card preparation. */
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t)SD_STORAGE_DISK ":", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_ok(fs_mkdir(SD_STORAGE_FILE_DIR));
    zassert_ok(fs_mkdir(SD_STORAGE_SETTINGS_DIR));
    zassert_ok(fs_unmount(&mount));
}

static void append_records(uint32_t count)
{
    for (uint32_t seq = 1; seq <= count; seq++) {
        zassert_equal(sd_interrupt_append(run, seq), SD_INTERRUPT_OK);
    }
}

static void end_boot(void)
{
    zassert_equal(crux_file_close(&run), CRUX_FILE_OK);
}

ZTEST(file_sd_interrupt, test_a_the_first_boot_has_no_previous_run)
{
    struct sd_interrupt_recovery recovery;

    provision();
    zassert_equal(sd_storage_start(), SD_STORAGE_READY);
    zassert_equal(sd_interrupt_begin(&recovery, &run), SD_INTERRUPT_OK);
    zassert_equal(recovery.boot, 1);
    zassert_false(recovery.previous_run_present);
    append_records(5);
    end_boot();
}

ZTEST(file_sd_interrupt, test_b_a_complete_previous_run_is_recovered_in_full)
{
    struct sd_interrupt_recovery recovery;

    zassert_equal(sd_interrupt_begin(&recovery, &run), SD_INTERRUPT_OK);
    zassert_equal(recovery.boot, 2);
    zassert_true(recovery.previous_run_present);
    zassert_equal(recovery.last_valid, 5);
    zassert_equal(recovery.torn_bytes, 0);
    zassert_false(recovery.damaged_record);
    append_records(3);
    /* A power cut mid-record leaves part of the next record behind. */
    uint8_t next[SD_INTERRUPT_RECORD_BYTES];
    size_t count = 0;
    sd_interrupt_make_record(4, next);
    zassert_equal(crux_file_write(run, next, 13, &count), CRUX_FILE_OK);
    zassert_equal(count, 13);
    end_boot();
}

ZTEST(file_sd_interrupt, test_c_a_partial_tail_is_reported_and_earlier_records_survive)
{
    struct sd_interrupt_recovery recovery;

    zassert_equal(sd_interrupt_begin(&recovery, &run), SD_INTERRUPT_OK);
    zassert_equal(recovery.boot, 3);
    zassert_equal(recovery.last_valid, 3);
    zassert_equal(recovery.torn_bytes, 13);
    zassert_false(recovery.damaged_record, "a short tail is a torn write, not a damaged record");
    append_records(4);
    end_boot();

    /* Flip one byte inside record 2 of the run just written (host-visible corruption). */
    crux_file_t *file = NULL;
    uint8_t byte = 0;
    size_t count = 0;
    zassert_equal(crux_file_open("run3", CRUX_FILE_READ_WRITE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_seek(file, SD_INTERRUPT_RECORD_BYTES + 10), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, &byte, 1, &count), CRUX_FILE_OK);
    byte ^= 0x01;
    zassert_equal(crux_file_seek(file, SD_INTERRUPT_RECORD_BYTES + 10), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, &byte, 1, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_sd_interrupt, test_d_a_damaged_full_record_stops_recovery_at_the_last_good_one)
{
    struct sd_interrupt_recovery recovery;

    zassert_equal(sd_interrupt_begin(&recovery, &run), SD_INTERRUPT_OK);
    zassert_equal(recovery.boot, 4);
    zassert_equal(recovery.last_valid, 1);
    zassert_equal(recovery.torn_bytes, 3 * SD_INTERRUPT_RECORD_BYTES);
    zassert_true(recovery.damaged_record);
    end_boot();
}

ZTEST(file_sd_interrupt, test_e_a_missing_previous_run_is_reported_not_invented)
{
    struct sd_interrupt_recovery recovery;

    zassert_equal(crux_file_remove("run4"), CRUX_FILE_OK);
    zassert_equal(sd_interrupt_begin(&recovery, &run), SD_INTERRUPT_OK);
    zassert_equal(recovery.boot, 5);
    zassert_false(recovery.previous_run_present);
    zassert_equal(recovery.last_valid, 0);
    end_boot();
}

ZTEST(file_sd_interrupt, test_f_an_invalid_counter_stops_without_creating_a_run)
{
    struct sd_interrupt_recovery recovery;
    crux_file_t *file = NULL;
    size_t count = 0;

    zassert_equal(crux_file_open("intcount", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "xyz", 3, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(sd_interrupt_begin(&recovery, &run), SD_INTERRUPT_COUNTER_INVALID);
    zassert_is_null(run, "no run file may be opened when the counter is unusable");
}

ZTEST_SUITE(file_sd_interrupt, NULL, NULL, NULL, NULL, NULL);
