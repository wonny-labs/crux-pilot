/* Classification logic of the replace-pattern power-cut test (samples/storage_sd_replace) on a RAM
 * disk named like the EVKB card. The test creates the states a power cut can leave behind; the real
 * cut is only exercised on the board. Tests run in name order: the File provider binds once. */
#include "sd_replace.h"
#include "sd_storage.h"
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

#define CFG SD_STORAGE_SETTINGS_DIR "/cfg"
#define CMP SD_STORAGE_SETTINGS_DIR "/cfg.cmp"

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

static void expect(const struct sd_replace_file *file, enum sd_replace_state state, uint32_t gen)
{
    zassert_equal(file->state, state);
    zassert_equal(file->gen, gen);
}

ZTEST(file_sd_replace, test_a_the_first_boot_finds_nothing)
{
    struct sd_replace_recovery recovery;

    provision();
    zassert_equal(sd_storage_start(), SD_STORAGE_READY);
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    zassert_equal(recovery.boot, 1);
    expect(&recovery.cfg, SD_REPLACE_NONE, 0);
    expect(&recovery.cmp, SD_REPLACE_NONE, 0);
}

ZTEST(file_sd_replace, test_b_a_generation_replaces_the_file_and_leaves_no_temporary)
{
    struct sd_replace_recovery recovery;

    zassert_equal(sd_replace_generation(1), SD_REPLACE_OK);
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    zassert_equal(recovery.boot, 2);
    expect(&recovery.cfg, SD_REPLACE_VALID, 1);
    expect(&recovery.cmp, SD_REPLACE_NONE, 0);

    zassert_equal(sd_replace_generation(2), SD_REPLACE_OK);
    zassert_equal(sd_replace_generation(3), SD_REPLACE_OK);
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    expect(&recovery.cfg, SD_REPLACE_VALID, 3);
    expect(&recovery.cmp, SD_REPLACE_NONE, 0);
}

ZTEST(file_sd_replace, test_c_a_cut_between_the_unlink_and_the_rename_is_classified)
{
    struct sd_replace_recovery recovery;

    zassert_equal(sd_replace_write(CMP, 4), SD_REPLACE_OK);
    zassert_ok(fs_unlink(CFG));
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    expect(&recovery.cfg, SD_REPLACE_NONE, 0);
    expect(&recovery.cmp, SD_REPLACE_VALID, 4);
    zassert_ok(fs_unlink(CMP));
}

ZTEST(file_sd_replace, test_d_a_cut_during_the_temporary_write_leaves_the_old_file_valid)
{
    struct sd_replace_recovery recovery;
    struct fs_file_t file;
    uint8_t record[SD_REPLACE_RECORD_BYTES];

    zassert_equal(sd_replace_generation(5), SD_REPLACE_OK);
    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, CMP, FS_O_CREATE | FS_O_WRITE));
    sd_replace_make_record(6, 0, record);
    zassert_equal(fs_write(&file, record, 20), 20); /* a partial first record */
    zassert_ok(fs_close(&file));
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    expect(&recovery.cfg, SD_REPLACE_VALID, 5);
    zassert_equal(recovery.cmp.state, SD_REPLACE_BAD);
    zassert_ok(fs_unlink(CMP));
}

ZTEST(file_sd_replace, test_e_a_truncated_file_is_bad_but_keeps_its_generation)
{
    struct sd_replace_recovery recovery;
    struct fs_file_t file;
    uint8_t record[SD_REPLACE_RECORD_BYTES];

    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, CFG, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
    for (uint32_t index = 0; index < 3; index++) { /* only 3 of the records */
        sd_replace_make_record(7, index, record);
        zassert_equal(fs_write(&file, record, sizeof(record)), sizeof(record));
    }
    zassert_ok(fs_close(&file));
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    expect(&recovery.cfg, SD_REPLACE_BAD, 7);
}

ZTEST(file_sd_replace, test_f_one_flipped_byte_makes_a_file_bad)
{
    struct sd_replace_recovery recovery;
    struct fs_file_t file;
    uint8_t byte = 0;

    zassert_equal(sd_replace_write(CFG, 8), SD_REPLACE_OK);
    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, CFG, FS_O_READ | FS_O_WRITE));
    zassert_ok(fs_seek(&file, 2 * SD_REPLACE_RECORD_BYTES + 12, FS_SEEK_SET));
    zassert_equal(fs_read(&file, &byte, 1), 1);
    byte ^= 0x01;
    zassert_ok(fs_seek(&file, 2 * SD_REPLACE_RECORD_BYTES + 12, FS_SEEK_SET));
    zassert_equal(fs_write(&file, &byte, 1), 1);
    zassert_ok(fs_close(&file));
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_OK);
    expect(&recovery.cfg, SD_REPLACE_BAD, 8);
}

ZTEST(file_sd_replace, test_g_an_invalid_counter_stops_before_anything_is_classified)
{
    struct sd_replace_recovery recovery;
    struct fs_file_t file;

    fs_file_t_init(&file);
    zassert_ok(
        fs_open(&file, SD_STORAGE_SETTINGS_DIR "/repcount", FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
    zassert_equal(fs_write(&file, "xyz", 3), 3);
    zassert_ok(fs_close(&file));
    zassert_equal(sd_replace_begin(&recovery), SD_REPLACE_COUNTER_INVALID);
}

ZTEST_SUITE(file_sd_replace, NULL, NULL, NULL, NULL, NULL);
