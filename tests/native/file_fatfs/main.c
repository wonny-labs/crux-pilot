#include "file_native.h"
#include "file_port.h"
#include <crux/file.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/RAM:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static void *setup(void)
{
    zassert_not_equal(fs_mount(&mount), 0, "unformatted media must not be formatted");
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_ok(fs_mkdir("/RAM:/files"));
    zassert_ok(fs_mkdir("/RAM:/settings"));
    zassert_equal(crux_file_zephyr_bind("/RAM:/files"), CRUX_FILE_OK);
    return NULL;
}

ZTEST(file_fatfs, test_maximum_length_name_roundtrip)
{
    char name[64];
    memset(name, 'a', sizeof(name) - 1);
    name[63] = '\0';
    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "ok", 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_open(name, CRUX_FILE_READ, &file), CRUX_FILE_OK);
    char value[2];
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_mem_equal(value, "ok", 2);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_distinct_portable_names_do_not_alias)
{
    const char *names[] = {"item", "ITEM", "item."};
    const char *values[] = {"a", "b", "c"};
    for (size_t i = 0; i < 3; ++i) {
        crux_file_t *file = NULL;
        size_t count = 0;
        zassert_equal(crux_file_open(names[i], CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
        zassert_equal(crux_file_write(file, values[i], 1, &count), CRUX_FILE_OK);
        zassert_equal(count, 1);
        zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    }
    for (size_t i = 0; i < 3; ++i) {
        crux_file_t *file = NULL;
        size_t count = 0;
        char value = 0;
        zassert_equal(crux_file_open(names[i], CRUX_FILE_READ, &file), CRUX_FILE_OK);
        zassert_equal(crux_file_read(file, &value, 1, &count), CRUX_FILE_OK);
        zassert_equal(count, 1);
        zassert_equal(value, values[i][0]);
        zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    }
}

ZTEST(file_fatfs, test_create_replace_truncates_and_sync_survives_remount)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[8] = {0};

    zassert_equal(crux_file_open("replace", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "old-value", 9, &count), CRUX_FILE_OK);
    zassert_equal(count, 9);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_equal(crux_file_open("replace", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "new", 3, &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_file_open("replace", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_mem_equal(value, "new", 3);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_read_write_seek_and_eof_bounds)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[6] = {0};

    zassert_equal(crux_file_open("seek", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "abcdef", 6, &count), CRUX_FILE_OK);
    zassert_equal(count, 6);
    zassert_equal(crux_file_seek(file, 2), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "XY", 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_equal(crux_file_seek(file, 0), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, sizeof(value));
    zassert_mem_equal(value, "abXYef", sizeof(value));
    zassert_equal(crux_file_seek(file, 7), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_seek(file, UINT64_MAX), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_access_mapping_handle_lifetime_and_remove)
{
    crux_file_t *file = NULL;
    size_t count = 0;

    zassert_equal(crux_file_open("missing", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_FOUND);
    zassert_is_null(file);
    zassert_equal(crux_file_open("access", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "x", 1, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_equal(crux_file_open("access", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "y", 1, &count), CRUX_FILE_ERR_ACCESS);
    zassert_equal(count, 0);
    zassert_equal(crux_file_remove("access"), CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_is_null(file);

    zassert_equal(crux_file_remove("access"), CRUX_FILE_OK);
    zassert_equal(crux_file_open("access", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_FOUND);
    zassert_is_null(file);
}

ZTEST(file_fatfs, test_file_api_uses_a_separate_settings_directory)
{
    struct fs_file_t settings_file;
    fs_file_t_init(&settings_file);
    zassert_ok(fs_open(&settings_file, "/RAM:/settings/marker", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&settings_file, "settings", 8), 8);
    zassert_ok(fs_sync(&settings_file));
    zassert_ok(fs_close(&settings_file));

    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open("marker", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "file", 4, &count), CRUX_FILE_OK);
    zassert_equal(count, 4);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    char value[8] = {0};
    fs_file_t_init(&settings_file);
    zassert_ok(fs_open(&settings_file, "/RAM:/settings/marker", FS_O_READ));
    zassert_equal(fs_read(&settings_file, value, sizeof(value)), 8);
    zassert_mem_equal(value, "settings", sizeof(value));
    zassert_ok(fs_close(&settings_file));
}

ZTEST(file_fatfs, test_file_handle_pool_limit_and_reuse)
{
    crux_file_t *files[CRUX_FILE_POOL_SIZE] = {0};
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; ++i) {
        char name[] = "pool0";
        name[4] = (char)('0' + i);
        zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &files[i]), CRUX_FILE_OK);
    }

    crux_file_t *extra = NULL;
    zassert_equal(crux_file_open("extra", CRUX_FILE_CREATE_REPLACE, &extra),
                  CRUX_FILE_ERR_NO_RESOURCES);
    zassert_is_null(extra);
    zassert_equal(crux_file_close(&files[1]), CRUX_FILE_OK);
    zassert_is_null(files[1]);
    zassert_equal(crux_file_open("reuse", CRUX_FILE_CREATE_REPLACE, &extra), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&extra), CRUX_FILE_OK);
    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; ++i) {
        if (files[i]) {
            zassert_equal(crux_file_close(&files[i]), CRUX_FILE_OK);
        }
    }
}

ZTEST(file_fatfs, test_unmounted_volume_is_not_ready)
{
    crux_file_t *file = NULL;
    zassert_ok(fs_unmount(&mount));
    zassert_equal(crux_file_open("offline", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
    zassert_is_null(file);
    zassert_ok(fs_mount(&mount));
}
static void write_file(const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, text, strlen(text), &count), CRUX_FILE_OK);
    zassert_equal(count, strlen(text));
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

static void expect_file(const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[32] = {0};
    zassert_true(strlen(text) < sizeof(value));
    zassert_equal(crux_file_open(name, CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, strlen(text));
    zassert_mem_equal(value, text, strlen(text));
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_read_write_mode_updates_in_place_and_never_creates)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[6] = {0};

    write_file("rw", "abcdef");
    zassert_equal(crux_file_open("rw", CRUX_FILE_READ_WRITE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "XY", 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_equal(crux_file_seek(file, 0), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, sizeof(value));
    zassert_mem_equal(value, "XYcdef", sizeof(value));
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_equal(crux_file_open("absent", CRUX_FILE_READ_WRITE, &file), CRUX_FILE_ERR_NOT_FOUND);
    zassert_is_null(file);
    zassert_equal(crux_file_open("absent", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_FOUND);
    zassert_equal(crux_file_remove("rw"), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_short_read_at_eof_returns_ok_with_the_available_count)
{
    crux_file_t *file = NULL;
    size_t count = 99;
    char value[8] = {0};

    write_file("eof", "abc");
    zassert_equal(crux_file_open("eof", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_mem_equal(value, "abc", 3);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 0);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_remove("eof"), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_storage_full_is_observable_and_space_is_reclaimed)
{
    static const uint8_t chunk[512] = {[0 ... 511] = 0x5a};
    crux_file_t *file = NULL;
    size_t count = 0;
    bool full = false;

    write_file("sentinel", "kept");
    zassert_equal(crux_file_open("filler", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    for (unsigned i = 0; i < 4096 && !full; i++) {
        crux_file_result_t rc = crux_file_write(file, chunk, sizeof(chunk), &count);
        if (rc == CRUX_FILE_OK) {
            continue;
        }
        /* The last chunk is either short or, when it fits exactly, refused outright. */
        zassert_true(rc == CRUX_FILE_PARTIAL || rc == CRUX_FILE_ERR_NO_SPACE);
        zassert_true(count < sizeof(chunk));
        full = true;
    }
    zassert_true(full, "a 2 MiB workload must exhaust a 1 MiB RAM disk");
    zassert_equal(crux_file_write(file, chunk, sizeof(chunk), &count), CRUX_FILE_ERR_NO_SPACE,
                  "a write that makes no progress on a full volume reports no space");
    zassert_equal(count, 0);
    crux_file_result_t closed = crux_file_close(&file);
    zassert_true(closed == CRUX_FILE_OK || closed == CRUX_FILE_ERR_NO_SPACE);
    zassert_is_null(file);

    zassert_equal(crux_file_remove("filler"), CRUX_FILE_OK);
    write_file("fresh", "new");
    expect_file("sentinel", "kept");
    expect_file("fresh", "new");
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    expect_file("sentinel", "kept");
    expect_file("fresh", "new");
    zassert_equal(crux_file_remove("sentinel"), CRUX_FILE_OK);
    zassert_equal(crux_file_remove("fresh"), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_corrupted_media_is_not_formatted_and_recovers_when_restored)
{
    uint8_t saved[512], now[512], zeros[512] = {0};
    crux_file_t *file = NULL;

    write_file("survivor", "kept");
    zassert_ok(fs_unmount(&mount));
    zassert_ok(disk_access_read("RAM", saved, 0, 1));
    zassert_ok(disk_access_write("RAM", zeros, 0, 1));

    zassert_true(fs_mount(&mount) < 0, "a volume without a boot sector must not mount");
    zassert_ok(disk_access_read("RAM", now, 0, 1));
    zassert_mem_equal(now, zeros, sizeof(now), "failed mount must not format");
    zassert_equal(crux_file_open("survivor", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
    zassert_is_null(file);
    zassert_equal(crux_file_remove("survivor"), CRUX_FILE_ERR_NOT_READY);

    zassert_ok(disk_access_write("RAM", saved, 0, 1));
    zassert_ok(fs_mount(&mount));
    expect_file("survivor", "kept");
    zassert_equal(crux_file_remove("survivor"), CRUX_FILE_OK);
}

static void fill_volume(void)
{
    static const uint8_t chunk[512] = {[0 ... 511] = 0xa5};
    crux_file_t *file = NULL;
    size_t count = 0;
    crux_file_result_t rc = CRUX_FILE_OK;

    zassert_equal(crux_file_open("filler", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    for (unsigned i = 0; i < 4096 && rc == CRUX_FILE_OK; i++) {
        rc = crux_file_write(file, chunk, sizeof(chunk), &count);
    }
    zassert_equal(rc, CRUX_FILE_ERR_NO_SPACE, "volume must report that it is full");
    (void)crux_file_close(&file);
}

ZTEST(file_fatfs, test_write_that_only_partly_fits_returns_partial_with_progress)
{
    static uint8_t big[4096];
    struct fs_statvfs stat;
    crux_file_t *file = NULL;
    size_t count = 0;

    memset(big, 0x33, sizeof(big));
    write_file("pad", "x"); /* owns exactly one cluster */
    fill_volume();
    zassert_equal(crux_file_remove("pad"), CRUX_FILE_OK);
    zassert_ok(fs_statvfs("/RAM:", &stat));
    zassert_equal(stat.f_bfree, 1, "removing the pad frees exactly one cluster");
    zassert_true(stat.f_frsize * 2 <= sizeof(big));

    zassert_equal(crux_file_open("last", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, big, stat.f_frsize * 2, &count), CRUX_FILE_PARTIAL);
    zassert_true(count > 0 && count < stat.f_frsize * 2);
    zassert_equal(crux_file_write(file, big, 1, &count), CRUX_FILE_ERR_NO_SPACE);
    zassert_equal(count, 0);
    (void)crux_file_close(&file);
    zassert_equal(crux_file_remove("last"), CRUX_FILE_OK);
    zassert_equal(crux_file_remove("filler"), CRUX_FILE_OK);
}

ZTEST(file_fatfs, test_creating_a_file_on_a_full_volume_reports_no_space_not_access)
{
    crux_file_t *file = NULL;
    size_t count = 0;

    fill_volume();
    crux_file_result_t rc = crux_file_open("late", CRUX_FILE_CREATE_REPLACE, &file);
    zassert_true(rc == CRUX_FILE_OK || rc == CRUX_FILE_ERR_NO_SPACE, "got %d", rc);
    if (rc == CRUX_FILE_OK) {
        zassert_equal(crux_file_write(file, "x", 1, &count), CRUX_FILE_ERR_NO_SPACE);
        zassert_equal(count, 0);
        (void)crux_file_close(&file);
        zassert_equal(crux_file_remove("late"), CRUX_FILE_OK);
    } else {
        zassert_is_null(file);
    }
    zassert_equal(crux_file_remove("filler"), CRUX_FILE_OK);
}

#define SECTOR 512
#define MAX_TORN_SECTORS 8
static uint8_t before_image[2048 * SECTOR];

/*
 * Interruption model: the media is captured around one sync and every subset of the
 * sectors that sync changed is replayed onto the earlier image, in any order. This
 * models a torn flush at sector granularity inside one process. It is not power-loss
 * evidence: no real media timing, controller cache or partial-sector write is involved.
 */
ZTEST(file_fatfs, test_torn_sync_images_mount_and_leave_unrelated_files_intact)
{
    static uint8_t after_sector[MAX_TORN_SECTORS][SECTOR];
    uint32_t changed[MAX_TORN_SECTORS];
    uint32_t sectors = 0, count_changed = 0;
    crux_file_t *file = NULL;
    size_t count = 0;
    const char *const old_value = "before";
    const char *const new_value = "after-longer-value";

    zassert_ok(disk_access_ioctl("RAM", DISK_IOCTL_GET_SECTOR_COUNT, &sectors));
    zassert_true(sectors * SECTOR <= sizeof(before_image));
    write_file("other", "stable");
    write_file("victim", old_value);
    zassert_ok(disk_access_read("RAM", before_image, 0, sectors));

    zassert_equal(crux_file_open("victim", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, new_value, strlen(new_value), &count), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    for (uint32_t s = 0; s < sectors; s++) {
        uint8_t now[SECTOR];
        zassert_ok(disk_access_read("RAM", now, s, 1));
        if (memcmp(now, &before_image[s * SECTOR], SECTOR) != 0) {
            zassert_true(count_changed < MAX_TORN_SECTORS, "sync changed too many sectors");
            changed[count_changed] = s;
            memcpy(after_sector[count_changed++], now, SECTOR);
        }
    }
    zassert_true(count_changed > 0, "a successful sync must reach the media");
    TC_PRINT("torn sync: %u changed sectors, %u images replayed\n", count_changed,
             1u << count_changed);
    (void)crux_file_close(&file);
    zassert_ok(fs_unmount(&mount));

    for (uint32_t subset = 0; subset < (1u << count_changed); subset++) {
        for (uint32_t i = 0; i < count_changed; i++) {
            const uint8_t *src =
                (subset & (1u << i)) ? after_sector[i] : &before_image[changed[i] * SECTOR];
            zassert_ok(disk_access_write("RAM", src, changed[i], 1));
        }
        zassert_ok(fs_mount(&mount), "torn image %u must still mount", subset);
        expect_file("other", "stable");
        if (subset == 0) {
            expect_file("victim", old_value);
        } else if (subset == (1u << count_changed) - 1) {
            expect_file("victim", new_value);
        } else {
            /* Integrity limit: a torn sync may leave the file's own bytes in any mix. */
            char value[32];
            zassert_equal(crux_file_open("victim", CRUX_FILE_READ, &file), CRUX_FILE_OK);
            zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
            zassert_true(count <= strlen(new_value));
            zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
        }
        zassert_ok(fs_unmount(&mount));
    }
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_file_remove("victim"), CRUX_FILE_OK);
    zassert_equal(crux_file_remove("other"), CRUX_FILE_OK);
}

ZTEST_SUITE(file_fatfs, NULL, setup, NULL, NULL, NULL);
