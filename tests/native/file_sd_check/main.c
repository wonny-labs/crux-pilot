/* The write/read/restart check of samples/storage_sd on a RAM disk named like the EVKB card. It
 * runs in name order: the File provider binds once per process. The remount reuses the in-process
 * RAM disk, so nothing here is persistence evidence; that needs the physical gate. */
#include "sd_file_check.h"
#include "sd_storage.h"
#include <crux/file.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

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

static void put(const char *name, const void *data, size_t length)
{
    crux_file_t *file = NULL;
    size_t count = 0;

    zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, data, length, &count), CRUX_FILE_OK);
    zassert_equal(count, length);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_sd_check, test_a_first_run_creates_the_counter_and_the_pattern)
{
    struct sd_file_check check = {0};

    provision();
    zassert_equal(sd_storage_start(), SD_STORAGE_READY);
    zassert_equal(sd_file_check_run(&check), SD_FILE_CHECK_OK);
    zassert_equal(check.boot_count, 1);
    zassert_true(check.pattern_created);
}

ZTEST(file_sd_check, test_b_a_second_run_counts_and_verifies_the_same_pattern)
{
    struct sd_file_check check = {0};

    zassert_equal(sd_file_check_run(&check), SD_FILE_CHECK_OK);
    zassert_equal(check.boot_count, 2);
    zassert_false(check.pattern_created, "an existing pattern must be verified, not rewritten");
}

ZTEST(file_sd_check, test_c_the_counter_survives_an_application_remount)
{
    struct sd_file_check check = {0};

    zassert_equal(sd_storage_stop(), 0);
    zassert_equal(sd_storage_remount(), 0);
    zassert_equal(sd_file_check_run(&check), SD_FILE_CHECK_OK);
    zassert_equal(check.boot_count, 3);
}

ZTEST(file_sd_check, test_d_one_flipped_pattern_byte_is_reported)
{
    struct sd_file_check check = {0};
    crux_file_t *file = NULL;
    size_t count = 0;
    uint8_t byte = 0;

    zassert_equal(crux_file_open("pattern", CRUX_FILE_READ_WRITE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_seek(file, 1000), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, &byte, 1, &count), CRUX_FILE_OK);
    zassert_equal(count, 1);
    byte ^= 0x01;
    zassert_equal(crux_file_seek(file, 1000), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, &byte, 1, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_equal(sd_file_check_run(&check), SD_FILE_CHECK_PATTERN_MISMATCH);
    zassert_equal(check.boot_count, 4, "the counter is still advanced before the pattern check");
    zassert_false(check.pattern_created);
}

ZTEST(file_sd_check, test_e_a_truncated_pattern_is_reported)
{
    struct sd_file_check check = {0};

    put("pattern", "short", 5);
    zassert_equal(sd_file_check_run(&check), SD_FILE_CHECK_PATTERN_MISMATCH);
}

ZTEST(file_sd_check, test_f_an_unreadable_counter_stops_the_check_without_writing)
{
    struct sd_file_check check = {0};
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[8] = {0};

    put("bootcount", "abc", 3);
    zassert_equal(sd_file_check_run(&check), SD_FILE_CHECK_COUNTER_INVALID);
    zassert_equal(crux_file_open("bootcount", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_mem_equal(value, "abc", 3, "an invalid counter must be left as found");
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST_SUITE(file_sd_check, NULL, NULL, NULL, NULL, NULL);
