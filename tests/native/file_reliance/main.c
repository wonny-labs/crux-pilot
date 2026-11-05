/* SPDX-License-Identifier: Apache-2.0 */
/* Reliance Edge behind the Zephyr VFS, on a RAM disk named like the eMMC disk. */
#include "fs_reliance_edge.h"
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

#define DISK "MMC"
#define DISK_SECTORS 8192U
#define SECTOR_BYTES 512U
#define BATCH_SECTORS 16U
#define BLOCK_BYTES 4096U

static struct fs_mount_t mnt = {
    .type = CRUX_FS_RELIANCE_EDGE,
    .mnt_point = "/MMC:",
    .storage_dev = (void *)DISK,
};

/* Same file system type, another disk name, and a mount point of another length, so that the VFS
 * does not refuse it before the adapter sees it. */
static struct fs_mount_t other = {
    .type = CRUX_FS_RELIANCE_EDGE,
    .mnt_point = "/OTHER:",
    .storage_dev = (void *)"OTHER",
};

/* Objects that after() closes, so that a failed assertion does not leave the volume busy for the
 * next test. */
static struct fs_file_t file;
static struct fs_file_t helper;
static struct fs_dir_t dir;

/* Faults at the disk access layer, counted per kind of call: the first `skip` matching calls pass,
 * the next `count` return `value`. A negative value fails without a transfer. A positive value
 * returns after the real transfer, as the SD stack can, so only the result marks the failure. */
enum disk_op { DISK_OP_NONE, DISK_OP_READ, DISK_OP_WRITE, DISK_OP_IOCTL };
static struct {
    enum disk_op op;
    uint32_t skip;
    uint32_t count;
    int value;
} fault;
static uint32_t write_calls;

static void fault_arm(enum disk_op op, uint32_t skip, uint32_t count, int value)
{
    fault.op = op;
    fault.skip = skip;
    fault.count = count;
    fault.value = value;
}

static void fault_clear(void)
{
    memset(&fault, 0, sizeof(fault));
}

static bool fault_take(enum disk_op op)
{
    if (fault.op != op || fault.count == 0U) {
        return false;
    }
    if (fault.skip > 0U) {
        fault.skip--;
        return false;
    }
    fault.count--;
    return true;
}

int __real_disk_access_read(const char *pdrv, uint8_t *data_buf, uint32_t start_sector,
                            uint32_t num_sector);
int __real_disk_access_write(const char *pdrv, const uint8_t *data_buf, uint32_t start_sector,
                             uint32_t num_sector);
int __real_disk_access_ioctl(const char *pdrv, uint8_t cmd, void *buff);

int __wrap_disk_access_read(const char *pdrv, uint8_t *data_buf, uint32_t start_sector,
                            uint32_t num_sector)
{
    if (fault_take(DISK_OP_READ)) {
        if (fault.value > 0) {
            (void)__real_disk_access_read(pdrv, data_buf, start_sector, num_sector);
        }
        return fault.value;
    }
    return __real_disk_access_read(pdrv, data_buf, start_sector, num_sector);
}

int __wrap_disk_access_write(const char *pdrv, const uint8_t *data_buf, uint32_t start_sector,
                             uint32_t num_sector)
{
    write_calls++;
    if (fault_take(DISK_OP_WRITE)) {
        if (fault.value > 0) {
            (void)__real_disk_access_write(pdrv, data_buf, start_sector, num_sector);
        }
        return fault.value;
    }
    return __real_disk_access_write(pdrv, data_buf, start_sector, num_sector);
}

int __wrap_disk_access_ioctl(const char *pdrv, uint8_t cmd, void *buff)
{
    if (fault_take(DISK_OP_IOCTL)) {
        if (fault.value > 0) {
            (void)__real_disk_access_ioctl(pdrv, cmd, buff);
        }
        return fault.value;
    }
    return __real_disk_access_ioctl(pdrv, cmd, buff);
}

static uint8_t batch[BATCH_SECTORS * SECTOR_BYTES];

/* FNV-1a over the whole disk, read below the wraps. */
static uint64_t medium_digest(void)
{
    uint64_t hash = 14695981039346656037ULL;
    for (uint32_t sector = 0; sector < DISK_SECTORS; sector += BATCH_SECTORS) {
        zassert_ok(__real_disk_access_read(DISK, batch, sector, BATCH_SECTORS));
        for (size_t i = 0; i < sizeof(batch); ++i) {
            hash ^= batch[i];
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

static void medium_fill(uint8_t value)
{
    memset(batch, value, sizeof(batch));
    for (uint32_t sector = 0; sector < DISK_SECTORS; sector += BATCH_SECTORS) {
        zassert_ok(__real_disk_access_write(DISK, batch, sector, BATCH_SECTORS));
    }
}

static void provision(void)
{
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)DISK, NULL, 0));
    zassert_ok(fs_mount(&mnt));
}

static void write_file(const char *path, const char *text)
{
    size_t length = strlen(text);
    fs_file_t_init(&helper);
    zassert_ok(fs_open(&helper, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
    zassert_equal(fs_write(&helper, text, length), (ssize_t)length);
    zassert_ok(fs_close(&helper));
}

static void expect_file(const char *path, const char *text)
{
    char value[64];
    size_t length = strlen(text);
    fs_file_t_init(&helper);
    zassert_ok(fs_open(&helper, path, FS_O_READ));
    zassert_equal(fs_read(&helper, value, sizeof(value)), (ssize_t)length);
    zassert_mem_equal(value, text, length);
    zassert_ok(fs_close(&helper));
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    fault_clear();
    write_calls = 0;
    mnt.flags = 0;
    fs_file_t_init(&file);
    fs_file_t_init(&helper);
    fs_dir_t_init(&dir);
}

static void after(void *fixture)
{
    ARG_UNUSED(fixture);
    fault_clear();
    (void)fs_close(&file);
    (void)fs_close(&helper);
    if (dir.mp != NULL) {
        (void)fs_closedir(&dir);
    }
    (void)fs_unmount(&other);
    (void)fs_unmount(&mnt);
    mnt.flags = 0;
}

ZTEST(file_reliance, test_blank_medium_mount_fails_and_digest_is_unchanged)
{
    medium_fill(0x00);
    uint64_t digest = medium_digest();
    zassert_true(fs_mount(&mnt) < 0, "a blank medium must not mount");
    zassert_equal(write_calls, 0, "a failed mount must not write");
    zassert_equal(medium_digest(), digest);

    /* A foreign pattern is not a volume either, and the first failure left nothing mounted. */
    medium_fill(0xA5);
    digest = medium_digest();
    write_calls = 0;
    int rc = fs_mount(&mnt);
    zassert_true(rc < 0 && rc != -EBUSY, "foreign medium mount returned %d", rc);
    zassert_equal(write_calls, 0);
    zassert_equal(medium_digest(), digest);
}

ZTEST(file_reliance, test_format_then_mount_and_unmount)
{
    medium_fill(0x00);
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)DISK, NULL, 0));
    zassert_ok(fs_mount(&mnt));
    write_file("/MMC:/kept", "kept");
    zassert_equal(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)DISK, NULL, 0), -EBUSY,
                  "a mounted volume must not be formatted");
    zassert_ok(fs_unmount(&mnt));

    zassert_ok(fs_mount(&mnt));
    expect_file("/MMC:/kept", "kept");
    zassert_ok(fs_unmount(&mnt));

    /* Formatting again gives an empty volume. */
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)DISK, NULL, 0));
    zassert_ok(fs_mount(&mnt));
    struct fs_dirent entry;
    zassert_equal(fs_stat("/MMC:/kept", &entry), -ENOENT);
    zassert_ok(fs_unmount(&mnt));
}

ZTEST(file_reliance, test_mount_point_itself_is_the_root)
{
    provision();
    write_file("/MMC:/top", "t");

    struct fs_dirent entry;
    zassert_ok(fs_stat("/MMC:", &entry));
    zassert_equal(entry.type, FS_DIR_ENTRY_DIR);

    /* The VFS answers the stat above itself; opendir and statvfs reach the adapter with nothing
     * after the mount point. */
    const char *roots[] = {"/MMC:", "/MMC:/"};
    for (size_t i = 0; i < ARRAY_SIZE(roots); ++i) {
        bool found = false;
        zassert_ok(fs_opendir(&dir, roots[i]), "opendir %s", roots[i]);
        for (;;) {
            zassert_ok(fs_readdir(&dir, &entry));
            if (entry.name[0] == '\0') {
                break;
            }
            if (strcmp(entry.name, "top") == 0) {
                found = true;
                zassert_equal(entry.type, FS_DIR_ENTRY_FILE);
                zassert_equal(entry.size, 1);
            }
        }
        zassert_ok(fs_closedir(&dir));
        zassert_true(found, "root listing of %s lacks the file", roots[i]);
    }

    struct fs_statvfs stat;
    zassert_ok(fs_statvfs("/MMC:", &stat));
    zassert_true(stat.f_blocks > 0);
}

ZTEST(file_reliance, test_second_mount_is_rejected)
{
    provision();
    zassert_equal(fs_mount(&other), -EBUSY);

    /* The first mount still reaches its own disk, through a commit and a remount. */
    write_file("/MMC:/still", "here");
    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    expect_file("/MMC:/still", "here");
}

ZTEST(file_reliance, test_unmount_with_an_open_handle_is_refused)
{
    provision();
    zassert_ok(fs_open(&file, "/MMC:/held", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_unmount(&mnt), -EBUSY);
    zassert_ok(fs_close(&file));

    zassert_ok(fs_opendir(&dir, "/MMC:"));
    zassert_equal(fs_unmount(&mnt), -EBUSY);
    zassert_ok(fs_closedir(&dir));

    zassert_ok(fs_unmount(&mnt));
}

ZTEST(file_reliance, test_open_flags_are_translated)
{
    char value[8];
    struct fs_dirent entry;
    provision();

    zassert_equal(fs_open(&file, "/MMC:/absent", FS_O_READ), -ENOENT);
    zassert_equal(fs_open(&file, "/MMC:/absent", FS_O_WRITE), -ENOENT);
    zassert_equal(fs_open(&file, "/MMC:/flags", 0), -EINVAL, "an open mode is required");

    /* Create creates; write-only refuses a read. */
    zassert_ok(fs_open(&file, "/MMC:/flags", FS_O_CREATE | FS_O_WRITE));
    zassert_true(fs_read(&file, value, 1) < 0);
    zassert_equal(fs_write(&file, "abc", 3), 3);
    zassert_ok(fs_close(&file));
    zassert_ok(fs_stat("/MMC:/flags", &entry));
    zassert_equal(entry.size, 3);

    /* Read-only refuses a write. */
    zassert_ok(fs_open(&file, "/MMC:/flags", FS_O_READ));
    zassert_true(fs_write(&file, "x", 1) < 0);
    zassert_equal(fs_read(&file, value, sizeof(value)), 3);
    zassert_ok(fs_close(&file));

    /* Read-write reads and writes through one handle. */
    zassert_ok(fs_open(&file, "/MMC:/flags", FS_O_RDWR));
    zassert_equal(fs_read(&file, value, 1), 1);
    zassert_equal(value[0], 'a');
    zassert_equal(fs_write(&file, "B", 1), 1);
    zassert_ok(fs_close(&file));
    expect_file("/MMC:/flags", "aBc");

    /* Append writes at the end whatever the position. */
    zassert_ok(fs_open(&file, "/MMC:/flags", FS_O_WRITE | FS_O_APPEND));
    zassert_ok(fs_seek(&file, 0, FS_SEEK_SET));
    zassert_equal(fs_write(&file, "d", 1), 1);
    zassert_ok(fs_close(&file));
    expect_file("/MMC:/flags", "aBcd");
}

ZTEST(file_reliance, test_read_write_seek_tell_truncate_stat_statvfs)
{
    uint8_t data[100];
    uint8_t value[32];
    struct fs_dirent entry;
    for (size_t i = 0; i < sizeof(data); ++i) {
        data[i] = (uint8_t)(i + 1U);
    }
    provision();

    zassert_ok(fs_open(&file, "/MMC:/rw", FS_O_CREATE | FS_O_RDWR));
    zassert_equal(fs_write(&file, data, sizeof(data)), (ssize_t)sizeof(data));
    zassert_equal(fs_tell(&file), 100);
    zassert_ok(fs_seek(&file, 10, FS_SEEK_SET));
    zassert_equal(fs_tell(&file), 10);
    zassert_equal(fs_read(&file, value, 5), 5);
    zassert_mem_equal(value, &data[10], 5);
    zassert_ok(fs_seek(&file, 5, FS_SEEK_CUR));
    zassert_equal(fs_tell(&file), 20);
    zassert_ok(fs_seek(&file, -10, FS_SEEK_END));
    zassert_equal(fs_tell(&file), 90);
    zassert_equal(fs_read(&file, value, 20), 10);
    zassert_mem_equal(value, &data[90], 10);
    zassert_equal(fs_read(&file, value, 1), 0);
    zassert_equal(fs_seek(&file, 0, 7), -EINVAL);
    zassert_true(fs_seek(&file, -1, FS_SEEK_SET) < 0);
    zassert_equal(fs_tell(&file), 100, "a rejected seek keeps the position");

    /* Counts that a Reliance Edge result cannot carry are refused before the call. */
    zassert_equal(fs_read(&file, value, (size_t)INT32_MAX + 1U), -EINVAL);
    zassert_equal(fs_write(&file, data, (size_t)INT32_MAX + 1U), -EINVAL);
#if SIZE_MAX > UINT32_MAX
    /* Truncated to 32 bits, these would be one-byte calls that succeed. */
    zassert_equal(fs_read(&file, value, (size_t)UINT32_MAX + 2U), -EINVAL);
    zassert_equal(fs_write(&file, data, (size_t)UINT32_MAX + 2U), -EINVAL);
#endif

    zassert_ok(fs_truncate(&file, 50));
    zassert_ok(fs_stat("/MMC:/rw", &entry));
    zassert_equal(entry.type, FS_DIR_ENTRY_FILE);
    zassert_equal(strcmp(entry.name, "rw"), 0, "stat name %s", entry.name);
    zassert_equal(entry.size, 50);
    zassert_ok(fs_truncate(&file, 70));
    zassert_ok(fs_stat("/MMC:/rw", &entry));
    zassert_equal(entry.size, 70);
    zassert_ok(fs_seek(&file, 40, FS_SEEK_SET));
    zassert_equal(fs_read(&file, value, sizeof(value)), 30);
    zassert_mem_equal(value, &data[40], 10);
    for (size_t i = 10; i < 30; ++i) {
        zassert_equal(value[i], 0, "extended byte %zu is %u", i, value[i]);
    }
    zassert_equal(fs_truncate(&file, -1), -EINVAL);
    zassert_ok(fs_sync(&file));
    zassert_ok(fs_close(&file));

    struct fs_statvfs before_write;
    struct fs_statvfs after_write;
    zassert_ok(fs_statvfs("/MMC:/rw", &before_write));
    zassert_equal(before_write.f_bsize, BLOCK_BYTES);
    zassert_equal(before_write.f_frsize, BLOCK_BYTES);
    zassert_true(before_write.f_blocks > 0 &&
                     before_write.f_blocks <= DISK_SECTORS * SECTOR_BYTES / BLOCK_BYTES,
                 "blocks %lu", before_write.f_blocks);
    zassert_true(before_write.f_bfree < before_write.f_blocks);

    static uint8_t chunk[BLOCK_BYTES];
    zassert_ok(fs_open(&file, "/MMC:/big", FS_O_CREATE | FS_O_WRITE));
    for (size_t i = 0; i < 16; ++i) {
        zassert_equal(fs_write(&file, chunk, sizeof(chunk)), (ssize_t)sizeof(chunk));
    }
    zassert_ok(fs_close(&file));
    zassert_ok(fs_statvfs("/MMC:", &after_write));
    zassert_true(after_write.f_bfree + 16 <= before_write.f_bfree, "free %lu then %lu",
                 before_write.f_bfree, after_write.f_bfree);
}

ZTEST(file_reliance, test_mkdir_readdir_unlink)
{
    struct fs_dirent entry;
    provision();

    zassert_ok(fs_mkdir("/MMC:/d"));
    zassert_ok(fs_stat("/MMC:/d", &entry));
    zassert_equal(entry.type, FS_DIR_ENTRY_DIR);
    write_file("/MMC:/d/x", "xx");
    write_file("/MMC:/d/y", "y");

    bool seen_x = false;
    bool seen_y = false;
    size_t entries = 0;
    zassert_ok(fs_opendir(&dir, "/MMC:/d"));
    for (;;) {
        zassert_ok(fs_readdir(&dir, &entry));
        if (entry.name[0] == '\0') {
            break;
        }
        entries++;
        zassert_equal(entry.type, FS_DIR_ENTRY_FILE);
        if (strcmp(entry.name, "x") == 0) {
            seen_x = true;
            zassert_equal(entry.size, 2);
        } else if (strcmp(entry.name, "y") == 0) {
            seen_y = true;
            zassert_equal(entry.size, 1);
        }
    }
    zassert_ok(fs_closedir(&dir));
    zassert_true(seen_x && seen_y && entries == 2, "listed %zu entries", entries);

    zassert_equal(fs_unlink("/MMC:/d"), -ENOTEMPTY);
    zassert_ok(fs_unlink("/MMC:/d/x"));
    zassert_equal(fs_stat("/MMC:/d/x", &entry), -ENOENT);
    zassert_ok(fs_unlink("/MMC:/d/y"));
    zassert_ok(fs_unlink("/MMC:/d"));
    zassert_equal(fs_stat("/MMC:/d", &entry), -ENOENT);

    zassert_ok(fs_mkdir("/MMC:/a"));
    zassert_ok(fs_mkdir("/MMC:/a/b"));
    zassert_ok(fs_stat("/MMC:/a/b", &entry));
    zassert_equal(entry.type, FS_DIR_ENTRY_DIR);
}

ZTEST(file_reliance, test_rename_replaces_an_existing_file)
{
    struct fs_dirent entry;
    provision();

    write_file("/MMC:/a", "new");
    write_file("/MMC:/b", "old!");
    zassert_ok(fs_rename("/MMC:/a", "/MMC:/b"));
    expect_file("/MMC:/b", "new");
    zassert_equal(fs_stat("/MMC:/a", &entry), -ENOENT);

    zassert_ok(fs_mkdir("/MMC:/sub"));
    zassert_ok(fs_rename("/MMC:/b", "/MMC:/sub/c"));
    zassert_equal(fs_stat("/MMC:/b", &entry), -ENOENT);

    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    expect_file("/MMC:/sub/c", "new");
}

ZTEST(file_reliance, test_error_values_are_zephyr_errno)
{
    struct fs_dirent entry;
    provision();

    zassert_equal(fs_stat("/MMC:/missing", &entry), -ENOENT);
    zassert_ok(fs_mkdir("/MMC:/e"));
    zassert_equal(fs_mkdir("/MMC:/e"), -EEXIST);
    write_file("/MMC:/e/f", "f");
    zassert_equal(fs_unlink("/MMC:/e"), -ENOTEMPTY);

    zassert_ok(fs_open(&file, "/MMC:/e/f", FS_O_READ));
    zassert_equal(fs_unmount(&mnt), -EBUSY);
    zassert_ok(fs_close(&file));

    /* Fill the volume: a short write may come first, then the error. */
    static uint8_t chunk[BLOCK_BYTES];
    ssize_t rc = 0;
    zassert_ok(fs_open(&file, "/MMC:/fill", FS_O_CREATE | FS_O_WRITE));
    for (size_t i = 0; i < 2U * DISK_SECTORS * SECTOR_BYTES / BLOCK_BYTES; ++i) {
        rc = fs_write(&file, chunk, sizeof(chunk));
        if (rc != (ssize_t)sizeof(chunk)) {
            break;
        }
    }
    if (rc >= 0) {
        rc = fs_write(&file, chunk, sizeof(chunk));
    }
    zassert_equal(rc, -ENOSPC);
    zassert_ok(fs_close(&file));
    zassert_ok(fs_unlink("/MMC:/fill"));
    zassert_ok(fs_unmount(&mnt));

    /* The adapter mounts read-only. The VFS flag is cleared after mounting so that a change reaches
     * the file system instead of being refused by the VFS. */
    mnt.flags = FS_MOUNT_FLAG_READ_ONLY;
    zassert_ok(fs_mount(&mnt));
    mnt.flags = 0;
    zassert_equal(fs_mkdir("/MMC:/ro"), -EROFS);
    zassert_equal(fs_unlink("/MMC:/e/f"), -EROFS);
    zassert_ok(fs_unmount(&mnt));

    /* A failed or a positive disk result while mounting is an I/O error. */
    fault_arm(DISK_OP_READ, 0, 1, -EIO);
    zassert_equal(fs_mount(&mnt), -EIO);
    fault_arm(DISK_OP_READ, 0, 1, 1);
    zassert_equal(fs_mount(&mnt), -EIO);
    fault_clear();
    zassert_ok(fs_mount(&mnt));
    expect_file("/MMC:/e/f", "f");
}

ZTEST_SUITE(file_reliance, NULL, NULL, before, after, NULL);
