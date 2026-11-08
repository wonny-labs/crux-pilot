/* SPDX-License-Identifier: Apache-2.0 */
/* Reliance Edge behind the Zephyr VFS, and the File provider on top of it, on a RAM disk named like
 * the eMMC disk. */
#include "conformance.h"
#include "file_native.h"
#include "file_port.h"
#include "fs_reliance_edge.h"
#include <crux/file.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>
#include <zephyr/ztest.h>

#define DISK "MMC"
#define DISK_SECTORS 8192U
#define SECTOR_BYTES 512U
#define BATCH_SECTORS 16U
#define BLOCK_BYTES 4096U
#define HANDLES CONFIG_CRUX_FS_RELIANCE_EDGE_HANDLE_COUNT
#define TASKS CONFIG_CRUX_FS_RELIANCE_EDGE_TASK_COUNT
/* The sectors of the volume: from the configured offset, and for the configured count or to the
 * end of the disk. */
#define VOLUME_FIRST CONFIG_CRUX_FS_RELIANCE_EDGE_SECTOR_OFFSET
#define VOLUME_SECTORS                                                                             \
    (CONFIG_CRUX_FS_RELIANCE_EDGE_SECTOR_COUNT != 0 ? CONFIG_CRUX_FS_RELIANCE_EDGE_SECTOR_COUNT    \
                                                    : DISK_SECTORS - VOLUME_FIRST)

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
static struct fs_file_t handles[HANDLES];

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
static uint32_t read_calls;
/* The lowest and highest sector that a write call touched, since the last reset. */
static uint32_t written_first = UINT32_MAX;
static uint32_t written_last;

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
    read_calls++;
    if (fault_take(DISK_OP_READ)) {
        if (fault.value > 0) {
            (void)__real_disk_access_read(pdrv, data_buf, start_sector, num_sector);
        }
        return fault.value;
    }
    return __real_disk_access_read(pdrv, data_buf, start_sector, num_sector);
}

/* The identifier of the calling thread, as Reliance Edge asks for it. The link wraps it, so that
 * a case can count how often Reliance Edge asks on behalf of one thread. */
uint32_t RedOsTaskId(void);
uint32_t __real_RedOsTaskId(void);
static k_tid_t asked_for;
static uint32_t asked_count;

uint32_t __wrap_RedOsTaskId(void)
{
    if (asked_for != NULL && asked_for == k_current_get()) {
        asked_count++;
    }
    return __real_RedOsTaskId();
}

/* A thread to stop at its next block write until the case lets it go, and the identifier that
 * Reliance Edge gets for it once it goes on. The wait is bounded, so that a case that fails while
 * the thread is stopped cannot leave the file system locked for the cases after it. */
static struct {
    k_tid_t thread;
    struct k_sem entered;
    struct k_sem release;
    uint32_t id_after_release;
} hold;

/* The threads whose identifiers are recorded at each block write. */
#define WATCHED_THREADS 2
#define WATCH_LOG_SIZE 128
static k_tid_t watched[WATCHED_THREADS];
static struct {
    size_t thread;
    uint32_t id;
} watch_log[WATCH_LOG_SIZE];
static size_t watch_count;

static void watch_write(void)
{
    k_tid_t self = k_current_get();

    if (hold.thread != NULL && hold.thread == self) {
        hold.thread = NULL;
        k_sem_give(&hold.entered);
        (void)k_sem_take(&hold.release, K_SECONDS(30));
        hold.id_after_release = RedOsTaskId();
    }
    for (size_t i = 0; i < WATCHED_THREADS; ++i) {
        if (watched[i] != NULL && watched[i] == self && watch_count < WATCH_LOG_SIZE) {
            watch_log[watch_count].thread = i;
            watch_log[watch_count].id = RedOsTaskId();
            watch_count++;
        }
    }
}

int __wrap_disk_access_write(const char *pdrv, const uint8_t *data_buf, uint32_t start_sector,
                             uint32_t num_sector)
{
    watch_write();
    write_calls++;
    if (num_sector > 0U) {
        written_first = MIN(written_first, start_sector);
        written_last = MAX(written_last, start_sector + num_sector - 1U);
    }
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
    /* Only the flush is faulted; the geometry queries of a mount pass. */
    if (cmd == DISK_IOCTL_CTRL_SYNC && fault_take(DISK_OP_IOCTL)) {
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
    read_calls = 0;
    mnt.flags = 0;
    fs_file_t_init(&file);
    fs_file_t_init(&helper);
    for (size_t i = 0; i < HANDLES; ++i) {
        fs_file_t_init(&handles[i]);
    }
    fs_dir_t_init(&dir);
}

static void after(void *fixture)
{
    ARG_UNUSED(fixture);
    fault_clear();
    (void)fs_close(&file);
    (void)fs_close(&helper);
    for (size_t i = 0; i < HANDLES; ++i) {
        (void)fs_close(&handles[i]);
    }
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

    /* Empty files take an inode but no data block. When the inodes run out, with blocks still
     * free, the file system's own error is "no inode left", and the VFS user sees no space. */
    struct fs_statvfs volume;
    char path[24];
    rc = 0;
    for (unsigned int i = 0; i < 4096U && rc == 0; ++i) {
        snprintk(path, sizeof(path), "/MMC:/n%u", i);
        rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
        if (rc == 0) {
            zassert_ok(fs_close(&file));
        }
    }
    zassert_equal(rc, -ENOSPC, "the inodes did not run out, or not as no space (%zd)", rc);
    zassert_ok(fs_statvfs("/MMC:", &volume));
    zassert_true(volume.f_bfree > 0, "the volume is full of blocks, not of inodes");
}

/* Unmount and mount again: the buffers are dropped, and a read-only volume can be written again. */
static void remount(void)
{
    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
}

/* A close that failed leaves the VFS object marked open, although the adapter has released the
 * slot, so the object is initialised again before it is used for another file. */
static void close_reports_loss(struct fs_file_t *f)
{
    zassert_equal(fs_close(f), -EROFS);
    fs_file_t_init(f);
}

/* After a media error, every change is refused, whatever handle or path it comes by. */
static void expect_volume_read_only(struct fs_file_t *open_handle)
{
    struct fs_file_t created;
    int rc;

    fs_file_t_init(&created);
    zassert_equal(fs_write(open_handle, "x", 1), -EROFS);
    zassert_equal(fs_sync(open_handle), -EROFS);
    zassert_equal(fs_mkdir("/MMC:/after-error"), -EROFS);
    rc = fs_open(&created, "/MMC:/created-after-error", FS_O_CREATE | FS_O_WRITE);
    if (rc == 0) {
        /* Release the slot so the teardown unmount is not blocked by the unexpected open. */
        (void)fs_close(&created);
    }
    zassert_equal(rc, -EROFS);
}

/* Handle B writes and its commit fails on the first block write, which makes the volume read-only.
 * B stays open with its unsaved write. */
static void fail_a_commit_on_b(void)
{
    zassert_ok(fs_open(&helper, "/MMC:/b", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&helper, "b", 1), 1);
    fault_arm(DISK_OP_WRITE, 0, 1, -EIO);
    zassert_equal(fs_sync(&helper), -EIO);
    zassert_equal(fault.count, 0U, "the commit must have reached the fault");
    fault_clear();
}

static void open_every_handle_and_no_more(void)
{
    char path[16];
    struct fs_file_t extra;
    int rc;

    fs_file_t_init(&extra);
    for (size_t i = 0; i < HANDLES; ++i) {
        snprintk(path, sizeof(path), "/MMC:/h%u", (unsigned int)i);
        zassert_ok(fs_open(&handles[i], path, FS_O_CREATE | FS_O_RDWR), "handle %zu of %u", i,
                   (unsigned int)HANDLES);
    }
    rc = fs_open(&extra, "/MMC:/hx", FS_O_CREATE | FS_O_RDWR);
    if (rc == 0) {
        /* Release the slot so the teardown unmount is not blocked by the unexpected open. */
        (void)fs_close(&extra);
    }
    zassert_equal(rc, -EMFILE);
    for (size_t i = 0; i < HANDLES; ++i) {
        zassert_ok(fs_close(&handles[i]));
    }
}

static uint8_t pattern[5U * BLOCK_BYTES];
static uint8_t readback[5U * BLOCK_BYTES + 1U];

static void write_pattern(struct fs_file_t *f, uint8_t byte, size_t length)
{
    memset(pattern, byte, length);
    zassert_equal(fs_write(f, pattern, length), (ssize_t)length);
}

/* The file holds one whole pattern: 0 for the previous one, 1 for the new one. */
static int which_pattern(const char *path, uint8_t previous, size_t previous_length, uint8_t next,
                         size_t next_length)
{
    fs_file_t_init(&helper);
    zassert_ok(fs_open(&helper, path, FS_O_READ));
    ssize_t got = fs_read(&helper, readback, sizeof(readback));
    zassert_ok(fs_close(&helper));
    zassert_true(got >= 0);
    if ((size_t)got == previous_length) {
        for (size_t i = 0; i < previous_length; ++i) {
            zassert_equal(readback[i], previous, "previous pattern torn at %zu", i);
        }
        return 0;
    }
    zassert_equal((size_t)got, next_length, "length %zd is neither version", got);
    for (size_t i = 0; i < next_length; ++i) {
        zassert_equal(readback[i], next, "new pattern torn at %zu", i);
    }
    return 1;
}

ZTEST(file_reliance, test_block_write_failure_makes_the_volume_read_only)
{
    provision();

    /* A commit whose first block write fails: the sync reports it, and nothing can change. */
    zassert_ok(fs_open(&file, "/MMC:/w", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&file, "data", 4), 4);
    fault_arm(DISK_OP_WRITE, 0, 1, -EIO);
    zassert_equal(fs_sync(&file), -EIO);
    zassert_equal(fault.count, 0U, "the commit must have reached the fault");
    expect_volume_read_only(&file);
    zassert_equal(fs_close(&file), -EROFS, "the unsaved write is lost, and close says so");
    fs_file_t_init(&file);
    remount();

    /* A write call that reaches the disk itself, through a buffer that has to be evicted: the call
     * that reaches the fault gets the error, and the volume is read-only after it. */
    static uint8_t chunk[BLOCK_BYTES];
    zassert_ok(fs_open(&file, "/MMC:/bulk", FS_O_CREATE | FS_O_WRITE));
    fault_arm(DISK_OP_WRITE, 0, 1, -EIO);
    ssize_t rc = (ssize_t)sizeof(chunk);
    for (size_t i = 0; i < 128U && rc == (ssize_t)sizeof(chunk); ++i) {
        rc = fs_write(&file, chunk, sizeof(chunk));
    }
    zassert_equal(fault.count, 0U, "the writes never reached the disk");
    zassert_equal(rc, -EIO);
    fault_clear();
    zassert_equal(fs_write(&file, chunk, sizeof(chunk)), -EROFS);
    zassert_equal(fs_sync(&file), -EROFS);
}

ZTEST(file_reliance, test_block_read_failure_makes_the_volume_read_only)
{
    char value[8];

    provision();
    write_file("/MMC:/a", "alpha");
    write_file("/MMC:/b", "beta");
    remount();

    /* Both are open before the fault, so that the open does not meet it. The buffers are cold. */
    zassert_ok(fs_open(&file, "/MMC:/a", FS_O_READ));
    zassert_ok(fs_open(&helper, "/MMC:/b", FS_O_RDWR));
    fault_arm(DISK_OP_READ, 0, 1, -EIO);
    zassert_equal(fs_read(&file, value, sizeof(value)), -EIO);
    zassert_equal(fault.count, 0U, "the read must have reached the fault");

    zassert_equal(fs_write(&helper, "x", 1), -EROFS);
    zassert_equal(fs_mkdir("/MMC:/after-error"), -EROFS);
    /* Reading is still attempted, and another file can be read. */
    zassert_equal(fs_read(&helper, value, sizeof(value)), 4);
    zassert_mem_equal(value, "beta", 4);

    /* Neither handle holds an unsaved write, so closing them is not an error. */
    zassert_ok(fs_close(&file));
    zassert_ok(fs_close(&helper));
    zassert_ok(fs_unmount(&mnt));
}

ZTEST(file_reliance, test_flush_failure_reports_and_remount_holds_a_complete_transaction)
{
    const size_t previous_length = 3U * BLOCK_BYTES;
    const size_t next_length = 5U * BLOCK_BYTES;
    const enum disk_op kinds[] = {DISK_OP_WRITE, DISK_OP_IOCTL};
    unsigned int outcomes[2] = {0, 0};

    provision();
    for (size_t k = 0; k < ARRAY_SIZE(kinds); ++k) {
        unsigned int failed_syncs = 0;
        bool reached = true;

        /* Fail the first, second, ... call of a commit, until a commit has fewer calls. */
        for (uint32_t skip = 0; skip < 16U && reached; ++skip) {
            zassert_ok(fs_open(&file, "/MMC:/t", FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
            write_pattern(&file, 'P', previous_length);
            zassert_ok(fs_close(&file));

            zassert_ok(fs_open(&file, "/MMC:/t", FS_O_WRITE | FS_O_TRUNC));
            write_pattern(&file, 'N', next_length);
            fault_arm(kinds[k], skip, 1, -EIO);
            int rc = fs_sync(&file);
            reached = (fault.count == 0U);
            fault_clear();

            if (reached) {
                zassert_equal(rc, -EIO, "kind %zu skip %u: sync returned %d", k, (unsigned)skip,
                              rc);
                failed_syncs++;
                zassert_equal(fs_write(&file, "x", 1), -EROFS);
                close_reports_loss(&file);
            } else {
                zassert_ok(rc);
                zassert_ok(fs_close(&file));
            }
            remount();
            outcomes[which_pattern("/MMC:/t", 'P', previous_length, 'N', next_length)]++;
        }
        zassert_true(failed_syncs >= 2U, "kind %zu: only %u commit calls were faulted", k,
                     failed_syncs);
    }
    /* The unfaulted commit at the end of each kind ends with the new content. */
    zassert_true(outcomes[1] >= 2U, "the new transaction never survived");
}

ZTEST(file_reliance, test_positive_disk_result_is_a_failure)
{
    char value[8];
    const struct {
        enum disk_op op;
        const char *name;
    } writes[] = {{DISK_OP_WRITE, "write"}, {DISK_OP_IOCTL, "flush"}};

    provision();
    write_file("/MMC:/r", "read");

    /* A positive result of a block write or of the flush fails the commit. */
    for (size_t i = 0; i < ARRAY_SIZE(writes); ++i) {
        zassert_ok(fs_open(&file, "/MMC:/p", FS_O_CREATE | FS_O_WRITE));
        zassert_equal(fs_write(&file, "data", 4), 4);
        fault_arm(writes[i].op, 0, 1, 1);
        zassert_equal(fs_sync(&file), -EIO, "a positive %s result is a failure", writes[i].name);
        zassert_equal(fault.count, 0U, "the %s fault was not reached", writes[i].name);
        fault_clear();
        expect_volume_read_only(&file);
        close_reports_loss(&file);
        remount();
    }

    /* So does a positive result of a block read, whose transfer did complete. */
    zassert_ok(fs_open(&file, "/MMC:/r", FS_O_READ));
    fault_arm(DISK_OP_READ, 0, 1, 1);
    zassert_equal(fs_read(&file, value, sizeof(value)), -EIO);
    zassert_equal(fault.count, 0U, "the read fault was not reached");
    fault_clear();
    zassert_equal(fs_mkdir("/MMC:/after-error"), -EROFS);
    zassert_ok(fs_close(&file));
}

ZTEST(file_reliance, test_close_with_unsaved_writes_on_read_only_volume_reports_erofs)
{
    provision();
    zassert_ok(fs_open(&file, "/MMC:/a", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&file, "aaa", 3), 3);
    fail_a_commit_on_b();

    zassert_equal(fs_close(&file), -EROFS, "A's write was lost, so its close must say so");
    /* The handle is released all the same. */
    zassert_equal(fs_close(&file), -EBADF);
    zassert_equal(fs_close(&helper), -EROFS);
    zassert_equal(fs_close(&helper), -EBADF);

    /* Nothing is leaked: the volume unmounts, and every handle is available again. */
    remount();
    open_every_handle_and_no_more();
}

ZTEST(file_reliance, test_close_after_a_good_sync_on_read_only_volume_reports_ok)
{
    provision();
    zassert_ok(fs_open(&file, "/MMC:/a", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&file, "aaa", 3), 3);
    zassert_ok(fs_sync(&file));
    fail_a_commit_on_b();

    zassert_ok(fs_close(&file), "A's write was saved, so its close is not an error");
    zassert_equal(fs_close(&helper), -EROFS);
    remount();
    expect_file("/MMC:/a", "aaa");
}

ZTEST(file_reliance, test_close_with_only_reads_on_read_only_volume_reports_ok)
{
    char value[8];

    provision();
    write_file("/MMC:/a", "alpha");
    zassert_ok(fs_open(&file, "/MMC:/a", FS_O_RDWR));
    zassert_equal(fs_read(&file, value, sizeof(value)), 5);
    fail_a_commit_on_b();

    zassert_ok(fs_close(&file), "a read-write handle that only read has nothing to lose");
    zassert_equal(fs_close(&helper), -EROFS);
    zassert_ok(fs_unmount(&mnt));
}

ZTEST(file_reliance, test_close_after_a_truncate_on_read_only_volume_reports_erofs)
{
    provision();
    write_file("/MMC:/a", "alpha");
    zassert_ok(fs_open(&file, "/MMC:/a", FS_O_RDWR));
    zassert_ok(fs_truncate(&file, 2));
    fail_a_commit_on_b();

    zassert_equal(fs_close(&file), -EROFS, "the truncation was lost, so its close must say so");
    zassert_equal(fs_close(&helper), -EROFS);
    remount();
    open_every_handle_and_no_more();
}

ZTEST(file_reliance, test_remount_after_a_critical_error_restores_writing)
{
    provision();
    write_file("/MMC:/old", "old");
    fail_a_commit_on_b();
    expect_volume_read_only(&helper);
    zassert_equal(fs_close(&helper), -EROFS);

    remount();
    write_file("/MMC:/new", "new");
    zassert_ok(fs_mkdir("/MMC:/dir"));
    remount();
    expect_file("/MMC:/old", "old");
    expect_file("/MMC:/new", "new");
}

ZTEST(file_reliance, test_read_only_mount_refuses_writes_and_never_writes_the_disk)
{
    char value[8];
    struct fs_dirent entry;

    provision();
    write_file("/MMC:/keep", "keep");
    zassert_ok(fs_unmount(&mnt));
    uint64_t digest = medium_digest();
    write_calls = 0;

    /* The VFS flag is cleared after mounting, so that a change reaches the adapter instead of being
     * refused by the VFS. */
    mnt.flags = FS_MOUNT_FLAG_READ_ONLY;
    zassert_ok(fs_mount(&mnt));
    mnt.flags = 0;
    expect_file("/MMC:/keep", "keep");

    zassert_equal(fs_open(&file, "/MMC:/keep", FS_O_WRITE), -EROFS);
    zassert_equal(fs_open(&file, "/MMC:/keep", FS_O_RDWR), -EROFS);
    zassert_equal(fs_open(&file, "/MMC:/keep", FS_O_WRITE | FS_O_TRUNC), -EROFS);
    zassert_equal(fs_open(&file, "/MMC:/new", FS_O_CREATE | FS_O_WRITE), -EROFS);
    zassert_equal(fs_mkdir("/MMC:/dir"), -EROFS);
    zassert_equal(fs_unlink("/MMC:/keep"), -EROFS);
    zassert_equal(fs_rename("/MMC:/keep", "/MMC:/moved"), -EROFS);

    /* A handle opened for reading cannot write, truncate or sync, and closes without error. */
    zassert_ok(fs_open(&file, "/MMC:/keep", FS_O_READ));
    /* The VFS does not check the open mode of write or truncate, so the adapter refuses these and
     * its errno is not pinned here: the looser check is deliberate. */
    zassert_true(fs_write(&file, "x", 1) < 0);
    zassert_true(fs_truncate(&file, 0) < 0);
    zassert_equal(fs_sync(&file), -EROFS);
    zassert_equal(fs_read(&file, value, sizeof(value)), 4);
    zassert_ok(fs_close(&file));
    zassert_ok(fs_stat("/MMC:/keep", &entry));
    zassert_equal(entry.size, 4);
    zassert_equal(fs_stat("/MMC:/new", &entry), -ENOENT);
    zassert_equal(fs_stat("/MMC:/moved", &entry), -ENOENT);

    zassert_ok(fs_unmount(&mnt));
    zassert_equal(write_calls, 0U, "a read-only mount must not write the disk");
    zassert_equal(medium_digest(), digest);
}

/* A file of more blocks than the direct pointers of an inode, so that truncating it reads the
 * blocks that point to the rest. */
#define BIG_BLOCKS 25U
static void write_big(const char *path)
{
    static uint8_t chunk[BLOCK_BYTES];

    memset(chunk, 0x5A, sizeof(chunk));
    zassert_ok(fs_open(&helper, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC));
    for (size_t i = 0; i < BIG_BLOCKS; ++i) {
        zassert_equal(fs_write(&helper, chunk, sizeof(chunk)), (ssize_t)sizeof(chunk));
    }
    zassert_ok(fs_close(&helper));
}

ZTEST(file_reliance, test_open_with_truncate_empties_the_file_and_keeps_every_slot)
{
    struct fs_dirent entry;

    provision();
    write_file("/MMC:/t", "content");
    zassert_ok(fs_open(&file, "/MMC:/t", FS_O_WRITE | FS_O_TRUNC));
    zassert_ok(fs_stat("/MMC:/t", &entry));
    zassert_equal(entry.size, 0U);
    zassert_equal(fs_write(&file, "x", 1), 1);
    zassert_ok(fs_close(&file));
    expect_file("/MMC:/t", "x");

    /* A truncating open that succeeded holds exactly one slot, released by its close. */
    for (size_t i = 0; i < HANDLES + 1U; ++i) {
        zassert_ok(fs_open(&file, "/MMC:/t", FS_O_WRITE | FS_O_TRUNC));
        zassert_ok(fs_close(&file));
    }
    open_every_handle_and_no_more();
    zassert_ok(fs_unmount(&mnt));
}

ZTEST(file_reliance, test_failing_truncate_on_open_does_not_leak_a_slot)
{
    struct fs_dirent entry;
    uint32_t reads_of_open;
    uint32_t reads_of_truncating_open;

    provision();
    write_big("/MMC:/t");

    /* Cold buffers each time, and the reads of a plain open and of a truncating open counted, so
     * that the fault below can land after the open and inside the truncation. */
    remount();
    read_calls = 0;
    zassert_ok(fs_open(&file, "/MMC:/t", FS_O_WRITE));
    reads_of_open = read_calls;
    zassert_ok(fs_close(&file));
    remount();
    read_calls = 0;
    zassert_ok(fs_open(&file, "/MMC:/t", FS_O_WRITE | FS_O_TRUNC));
    reads_of_truncating_open = read_calls;
    zassert_ok(fs_close(&file));
    zassert_true(reads_of_truncating_open > reads_of_open,
                 "truncation reads %u blocks beyond the open's %u, or the fault cannot land in it",
                 (unsigned)reads_of_truncating_open, (unsigned)reads_of_open);

    write_big("/MMC:/t");
    remount();
    fault_arm(DISK_OP_READ, reads_of_open, 1, -EIO);
    zassert_equal(fs_open(&file, "/MMC:/t", FS_O_WRITE | FS_O_TRUNC), -EIO);
    zassert_equal(fault.count, 0U, "the truncation did not reach the fault");
    fault_clear();
    zassert_is_null(file.mp, "a failed open leaves no handle");

    /* The VFS does not call close after a failed open, so the adapter must hold nothing: the
     * volume unmounts, and every handle can be opened. */
    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    zassert_ok(fs_stat("/MMC:/t", &entry));
    zassert_equal(entry.size, BIG_BLOCKS * BLOCK_BYTES, "the failed truncation changed the file");
    open_every_handle_and_no_more();
    zassert_ok(fs_unmount(&mnt));
}

ZTEST_SUITE(file_reliance, NULL, NULL, before, after, NULL);

/*
 * The File provider on the same volume. The roots are /MMC:/a and /MMC:/b, bound once for the
 * whole process, so the suite formats and mounts once and every case leaves the volume mounted and
 * the two roots without files. The RAM disk is in this process, so a remount here is not
 * persistence evidence.
 */
static void rebuild_volume(void)
{
    const char *const directories[] = {"/MMC:/a",    "/MMC:/a/inner", "/MMC:/b",
                                       "/MMC:/free", "/MMC:/a2",      "/MMC:/settings"};

    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)DISK, NULL, 0));
    zassert_ok(fs_mount(&mnt));
    for (size_t i = 0; i < ARRAY_SIZE(directories); ++i) {
        zassert_ok(fs_mkdir(directories[i]), "%s", directories[i]);
    }
}

static void put_file(const char *name, const char *text);
static void expect_content(const char *name, const char *text);

/* A name that fails the File name rule, so that it can sit next to File's own files. */
static void raw_file(crux_file_root_t root, const char *leaf)
{
    char path[96];
    struct fs_file_t raw;

    fs_file_t_init(&raw);
    zassert_true(snprintk(path, sizeof(path), "/MMC:/%c/%s", 'a' + root, leaf) < (int)sizeof(path));
    zassert_ok(fs_open(&raw, path, FS_O_CREATE | FS_O_WRITE), "%s", leaf);
    zassert_ok(fs_close(&raw));
}

static void add_directory(crux_file_root_t root, const char *name)
{
    char path[96];

    zassert_true(snprintk(path, sizeof(path), "/MMC:/%c/%s", 'a' + root, name) < (int)sizeof(path));
    zassert_ok(fs_mkdir(path));
}

static void add_foreign_files(crux_file_root_t root)
{
    raw_file(root, "with space");
    raw_file(root, "name!");
    raw_file(root, "caf\xc3\xa9");
    raw_file(root, "tab\there");
    raw_file(root, "a:b");
}

static const crux_file_conformance_env_t env = {
    .add_directory = add_directory,
    .add_foreign_files = add_foreign_files,
    .bind_root = crux_file_zephyr_bind_root,
    .path_0 = "/MMC:/a",
    .path_0_parent = "/MMC:",
    .path_0_child = "/MMC:/a/inner",
    .path_free = "/MMC:/free",
    .path_0_prefix = "/MMC:/a2",
};

static void *file_setup(void)
{
    /* Whatever an earlier suite left on the medium, a blank one is not a volume. */
    (void)fs_unmount(&mnt);
    medium_fill(0x00);
    zassert_not_equal(fs_mount(&mnt), 0, "an unformatted medium must not mount");
    rebuild_volume();
    k_sem_init(&hold.entered, 0, 1);
    k_sem_init(&hold.release, 0, 1);
    zassert_equal(crux_file_zephyr_bind("/MMC:/a"), CRUX_FILE_OK, "the original bind is root 0");
    zassert_equal(crux_file_zephyr_bind_root(1, "/MMC:/b"), CRUX_FILE_OK);
    return NULL;
}

static void file_before(void *fixture)
{
    ARG_UNUSED(fixture);
    fault_clear();
    write_calls = 0;
    read_calls = 0;
}

/* Remove every entry of a root except the directory that the conformance cases expect in root 0,
 * so that the next case starts from a root without files. */
static void empty_root(const char *root)
{
    struct fs_dir_t listing;
    struct fs_dirent entry;
    char path[96];

    for (unsigned int guard = 0; guard < 4096U; ++guard) {
        fs_dir_t_init(&listing);
        if (fs_opendir(&listing, root) != 0) {
            return;
        }
        bool removed = false;
        while (fs_readdir(&listing, &entry) == 0 && entry.name[0] != '\0') {
            if (strcmp(entry.name, "inner") != 0) {
                snprintk(path, sizeof(path), "%s/%s", root, entry.name);
                removed = fs_unlink(path) == 0;
                break;
            }
        }
        (void)fs_closedir(&listing);
        if (!removed) {
            return;
        }
    }
}

static void thread_cases_cleanup(void);

/* A File handle that a case leaves to the teardown if it fails while the handle is open: an
 * open handle keeps the volume from being unmounted, and so from being made writable again. */
static crux_file_t *kept_file;

/* Leave a mounted volume with empty roots, whatever the case did or where it failed. */
static void file_after(void *fixture)
{
    struct fs_dirent entry;

    ARG_UNUSED(fixture);
    fault_clear();
    thread_cases_cleanup();
    if (kept_file != NULL) {
        (void)crux_file_close(&kept_file);
    }
    /* A media error leaves the volume read-only until it is mounted again, so it is mounted again
     * whatever the case did, and a case that failed before its own remount does not fail the cases
     * after it. A volume that does not mount, or has lost its roots, is formatted again. */
    (void)fs_unmount(&mnt);
    (void)fs_mount(&mnt);
    if (fs_stat("/MMC:/a", &entry) != 0) {
        (void)fs_unmount(&mnt);
        rebuild_volume();
    }
    empty_root("/MMC:/a");
    empty_root("/MMC:/b");
}

static void file_teardown(void *fixture)
{
    ARG_UNUSED(fixture);
    (void)fs_unmount(&mnt);
}

static void put_file(const char *name, const char *text)
{
    crux_file_t *file = NULL;
    size_t count = 0;

    zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, text, strlen(text), &count), CRUX_FILE_OK);
    zassert_equal(count, strlen(text));
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

static void expect_content(const char *name, const char *text)
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

ZTEST(file_reliance_file, test_maximum_length_name_roundtrip)
{
    char name[64];
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[2];

    memset(name, 'a', sizeof(name) - 1);
    name[63] = '\0';
    zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "ok", 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_open(name, CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_mem_equal(value, "ok", 2);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    /* A name one character shorter is a different file: truncation or aliasing would merge them. */
    put_file(name + 1, "zz");
    expect_content(name, "ok");
    expect_content(name + 1, "zz");
}

ZTEST(file_reliance_file, test_distinct_portable_names_do_not_alias)
{
    const char *names[] = {"item", "ITEM", "item."};
    const char *values[] = {"a", "b", "c"};

    for (size_t i = 0; i < 3; ++i) {
        put_file(names[i], values[i]);
    }
    for (size_t i = 0; i < 3; ++i) {
        expect_content(names[i], values[i]);
    }
}

ZTEST(file_reliance_file,
      test_create_replace_truncates_and_data_is_visible_after_in_process_remount)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[8] = {0};

    put_file("replace", "old-value");
    zassert_equal(crux_file_open("replace", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "new", 3, &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    zassert_equal(crux_file_open("replace", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_mem_equal(value, "new", 3);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_reliance_file, test_read_write_seek_and_eof_bounds)
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

ZTEST(file_reliance_file, test_access_mapping_handle_lifetime_and_remove)
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

ZTEST(file_reliance_file, test_file_api_uses_a_separate_settings_directory)
{
    struct fs_file_t settings_file;
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[8] = {0};

    fs_file_t_init(&settings_file);
    zassert_ok(fs_open(&settings_file, "/MMC:/settings/marker", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&settings_file, "settings", 8), 8);
    zassert_ok(fs_sync(&settings_file));
    zassert_ok(fs_close(&settings_file));

    zassert_equal(crux_file_open("marker", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "file", 4, &count), CRUX_FILE_OK);
    zassert_equal(count, 4);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    fs_file_t_init(&settings_file);
    zassert_ok(fs_open(&settings_file, "/MMC:/settings/marker", FS_O_READ));
    zassert_equal(fs_read(&settings_file, value, sizeof(value)), 8);
    zassert_mem_equal(value, "settings", sizeof(value));
    zassert_ok(fs_close(&settings_file));
    zassert_ok(fs_unlink("/MMC:/settings/marker"));
}

ZTEST(file_reliance_file, test_file_handle_pool_limit_and_reuse)
{
    crux_file_t *files[CRUX_FILE_POOL_SIZE] = {0};
    crux_file_t *extra = NULL;

    for (size_t i = 0; i < CRUX_FILE_POOL_SIZE; ++i) {
        char name[] = "pool0";
        name[4] = (char)('0' + i);
        zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &files[i]), CRUX_FILE_OK);
    }
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

ZTEST(file_reliance_file, test_unmounted_volume_is_not_ready)
{
    crux_file_t *file = NULL;

    zassert_ok(fs_unmount(&mnt));
    zassert_equal(crux_file_open("offline", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
    zassert_is_null(file);
    zassert_ok(fs_mount(&mnt));
}

ZTEST(file_reliance_file, test_read_write_mode_updates_in_place_and_never_creates)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[6] = {0};

    put_file("rw", "abcdef");
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

ZTEST(file_reliance_file, test_short_read_at_eof_returns_ok_with_the_available_count)
{
    crux_file_t *file = NULL;
    size_t count = 99;
    char value[8] = {0};

    put_file("eof", "abc");
    zassert_equal(crux_file_open("eof", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 3);
    zassert_mem_equal(value, "abc", 3);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 0);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_remove("eof"), CRUX_FILE_OK);
}

/* Writes whole blocks to a new file until the volume refuses. The result of the last write that
 * made no progress is returned, and the file is closed. */
#define FILL_CHUNKS (2U * DISK_SECTORS * SECTOR_BYTES / BLOCK_BYTES)
static void fill_volume(const char *name)
{
    static const uint8_t chunk[BLOCK_BYTES] = {[0 ... BLOCK_BYTES - 1] = 0xa5};
    crux_file_t *file = NULL;
    size_t count = 0;
    crux_file_result_t rc = CRUX_FILE_OK;

    zassert_equal(crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    for (unsigned int i = 0; i < FILL_CHUNKS && rc == CRUX_FILE_OK; i++) {
        rc = crux_file_write(file, chunk, sizeof(chunk), &count);
    }
    /* The last chunk is either short or, when it fits exactly, refused outright. */
    zassert_true(rc == CRUX_FILE_PARTIAL || rc == CRUX_FILE_ERR_NO_SPACE,
                 "the volume must report that it is full, got %d", rc);
    zassert_equal(crux_file_write(file, chunk, sizeof(chunk), &count), CRUX_FILE_ERR_NO_SPACE,
                  "a write that makes no progress on a full volume reports no space");
    zassert_equal(count, 0);
    crux_file_result_t closed = crux_file_close(&file);
    zassert_true(closed == CRUX_FILE_OK || closed == CRUX_FILE_ERR_NO_SPACE, "close gave %d",
                 closed);
    zassert_is_null(file);
}

ZTEST(file_reliance_file, test_storage_full_is_observable_and_space_is_reclaimed)
{
    struct fs_statvfs before_removal;
    struct fs_statvfs after_removal;

    put_file("sentinel", "kept");
    fill_volume("filler");

    zassert_ok(fs_statvfs("/MMC:", &before_removal));
    zassert_equal(crux_file_remove("filler"), CRUX_FILE_OK);
    zassert_ok(fs_statvfs("/MMC:", &after_removal));
    zassert_true(after_removal.f_bfree > before_removal.f_bfree,
                 "removal must free blocks (%lu then %lu)", (unsigned long)before_removal.f_bfree,
                 (unsigned long)after_removal.f_bfree);
    put_file("fresh", "new");
    expect_content("sentinel", "kept");
    expect_content("fresh", "new");
    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    expect_content("sentinel", "kept");
    expect_content("fresh", "new");
}

ZTEST(file_reliance_file, test_corrupted_media_is_not_formatted_and_recovers_when_restored)
{
    static uint8_t saved[BLOCK_BYTES];
    static uint8_t zeros[BLOCK_BYTES];
    static uint8_t now[BLOCK_BYTES];
    crux_file_t *file = NULL;

    put_file("survivor", "kept");
    zassert_ok(fs_unmount(&mnt));
    /* The first block of the volume holds what identifies it. */
    zassert_ok(__real_disk_access_read(DISK, saved, VOLUME_FIRST, BLOCK_BYTES / SECTOR_BYTES));
    zassert_ok(__real_disk_access_write(DISK, zeros, VOLUME_FIRST, BLOCK_BYTES / SECTOR_BYTES));
    write_calls = 0;

    zassert_true(fs_mount(&mnt) < 0, "a volume without its first block must not mount");
    zassert_equal(write_calls, 0, "a failed mount must not write");
    zassert_ok(__real_disk_access_read(DISK, now, VOLUME_FIRST, BLOCK_BYTES / SECTOR_BYTES));
    zassert_mem_equal(now, zeros, sizeof(now), "failed mount must not format");
    zassert_equal(crux_file_open("survivor", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
    zassert_is_null(file);
    zassert_equal(crux_file_remove("survivor"), CRUX_FILE_ERR_NOT_READY);

    zassert_ok(__real_disk_access_write(DISK, saved, VOLUME_FIRST, BLOCK_BYTES / SECTOR_BYTES));
    zassert_ok(fs_mount(&mnt));
    expect_content("survivor", "kept");
}

ZTEST(file_reliance_file, test_write_that_only_partly_fits_returns_partial_with_progress)
{
    static uint8_t big[5U * BLOCK_BYTES];
    struct fs_statvfs volume;
    crux_file_t *file = NULL;
    size_t count = 0;

    memset(big, 0x33, sizeof(big));
    put_file("pad", "x"); /* owns one data block */
    fill_volume("filler");
    zassert_equal(crux_file_remove("pad"), CRUX_FILE_OK);
    zassert_ok(fs_statvfs("/MMC:", &volume));
    zassert_true(volume.f_bfree >= 1 && volume.f_bfree <= 3, "removing the pad freed %lu blocks",
                 (unsigned long)volume.f_bfree);
    size_t request = (volume.f_bfree + 1U) * volume.f_frsize;
    zassert_true(request <= sizeof(big));

    zassert_equal(crux_file_open("last", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, big, request, &count), CRUX_FILE_PARTIAL);
    zassert_true(count > 0 && count < request, "accepted %zu of %zu", count, request);
    zassert_equal(crux_file_write(file, big, 1, &count), CRUX_FILE_ERR_NO_SPACE);
    zassert_equal(count, 0);
    (void)crux_file_close(&file);
}

ZTEST(file_reliance_file, test_creating_a_file_on_a_full_volume_reports_no_space_not_access)
{
    crux_file_t *file = NULL;
    size_t count = 0;

    fill_volume("filler");
    crux_file_result_t rc = crux_file_open("late", CRUX_FILE_CREATE_REPLACE, &file);
    zassert_true(rc == CRUX_FILE_OK || rc == CRUX_FILE_ERR_NO_SPACE, "got %d", rc);
    if (rc == CRUX_FILE_OK) {
        zassert_equal(crux_file_write(file, "x", 1, &count), CRUX_FILE_ERR_NO_SPACE);
        zassert_equal(count, 0);
        (void)crux_file_close(&file);
    } else {
        zassert_is_null(file);
    }
}

ZTEST(file_reliance_file, test_creating_empty_files_on_an_exhausted_volume_reports_no_space)
{
    crux_file_t *file = NULL;
    crux_file_result_t last = CRUX_FILE_OK;
    unsigned int created = 0;
    bool failed = false;
    char name[48];

    fill_volume("filler");
    /* Empty files need no data block, and the first creation may already find no block for the
     * directory. Whichever limit ends the loop, the answer is no space, never an access error. */
    for (unsigned int i = 0; i < 4096U && !failed; i++) {
        snprintf(name, sizeof(name), "%040u", i);
        crux_file_result_t rc = crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file);
        if (rc == CRUX_FILE_OK) {
            zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
            created++;
        } else {
            failed = true;
            last = rc;
        }
    }
    zassert_true(failed, "4096 empty creations on a full volume never failed");
    zassert_equal(last, CRUX_FILE_ERR_NO_SPACE, "creation failed with %d after %u files", last,
                  created);
}

/* The same limit reached by inodes: blocks are free, and the answer is still no space. */
ZTEST(file_reliance_file, test_creating_empty_files_until_no_inode_is_left_reports_no_space)
{
    struct fs_statvfs volume;
    crux_file_t *file = NULL;
    crux_file_result_t rc = CRUX_FILE_OK;
    unsigned int created = 0;
    char name[48];

    for (unsigned int i = 0; i < 4096U && rc == CRUX_FILE_OK; i++) {
        snprintf(name, sizeof(name), "%040u", i);
        rc = crux_file_open(name, CRUX_FILE_CREATE_REPLACE, &file);
        if (rc == CRUX_FILE_OK) {
            zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
            created++;
        }
    }
    TC_PRINT("%u empty files were created\n", created);
    zassert_equal(rc, CRUX_FILE_ERR_NO_SPACE, "creation failed with %d after %u files", rc,
                  created);
    zassert_true(created > 0);
    zassert_ok(fs_statvfs("/MMC:", &volume));
    zassert_true(volume.f_bfree > 0, "the volume is full of blocks, not of inodes");
}

ZTEST(file_reliance_file, test_bind_rejects_invalid_roots_and_a_second_bind)
{
    char too_long[80];

    memset(too_long, 'a', sizeof(too_long));
    too_long[0] = '/';
    too_long[sizeof(too_long) - 1] = '\0';
    zassert_equal(crux_file_zephyr_bind(NULL), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_zephyr_bind("files"), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_zephyr_bind("/"), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_zephyr_bind("/MMC:/a/"), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_zephyr_bind(too_long), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_zephyr_bind("/MMC:/settings"), CRUX_FILE_ERR_INVALID_STATE,
                  "the root is bound once");
}

ZTEST(file_reliance_file, test_seek_to_eof_is_allowed_and_a_rejected_seek_keeps_the_position)
{
    crux_file_t *file = NULL;
    size_t count = 99;
    char value[4] = {0};

    put_file("pos", "abcdef");
    zassert_equal(crux_file_open("pos", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_seek(file, 6), CRUX_FILE_OK, "EOF itself is a valid offset");
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 0);
    zassert_equal(crux_file_seek(file, 2), CRUX_FILE_OK);
    zassert_equal(crux_file_seek(file, 7), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_read(file, value, 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_mem_equal(value, "cd", 2, "a rejected seek must not move the position");
    /* Above 32-bit offsets the File core still accepts the value; the provider rejects it. */
    zassert_equal(crux_file_seek(file, (uint64_t)INT32_MAX + 1), CRUX_FILE_ERR_OUT_OF_RANGE);
    zassert_equal(crux_file_read(file, value, 1, &count), CRUX_FILE_OK);
    zassert_equal(count, 1);
    zassert_equal(value[0], 'e');
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_reliance_file, test_removing_a_missing_file_reports_not_found)
{
    zassert_equal(crux_file_remove("never-created"), CRUX_FILE_ERR_NOT_FOUND);
}

/* One case, because the conformance sequence leaves entries that its later steps expect. */
ZTEST(file_reliance_file, test_conformance_on_reliance_edge)
{
    crux_file_conformance_roots(&env);
    crux_file_conformance_stat(&env);
    crux_file_conformance_size();
    crux_file_conformance_rename(&env);
    crux_file_conformance_listing(&env);
    crux_file_conformance_binding(&env);
}

ZTEST(file_reliance_file, test_a_rename_survives_a_remount_and_never_leaves_two_names)
{
    crux_file_t *file = NULL;
    crux_file_info_t info;
    size_t count = 0;

    zassert_equal(crux_file_open_at(1, "before", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "data", 4, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_rename(1, "before", "after"), CRUX_FILE_OK);

    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    zassert_equal(crux_file_stat(1, "before", &info), CRUX_FILE_ERR_NOT_FOUND);
    zassert_equal(crux_file_stat(1, "after", &info), CRUX_FILE_OK);
    zassert_equal(info.size, 4);
}

/* Reliance Edge's own rename replaces a destination, so this is the check that matters. */
ZTEST(file_reliance_file, test_rename_keeps_an_existing_destination)
{
    crux_file_t *file = NULL;
    crux_file_info_t info;
    size_t count = 0;

    zassert_equal(crux_file_open_at(1, "src", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(crux_file_open_at(1, "dst", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "kept", 4, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);

    zassert_equal(crux_file_rename(1, "src", "dst"), CRUX_FILE_ERR_EXISTS);

    zassert_equal(crux_file_stat(1, "dst", &info), CRUX_FILE_OK);
    zassert_equal(info.size, 4);
    zassert_equal(crux_file_stat(1, "src", &info), CRUX_FILE_OK);
}

ZTEST(file_reliance_file, test_listing_skips_directories_and_foreign_names)
{
    const char *names[] = {"a1", "Item", "item", "z.b", "0123456789abcdefghij"};
    bool seen[ARRAY_SIZE(names)] = {false};
    crux_file_dir_t *dir = NULL;
    crux_file_info_t info;
    char name[64];
    bool end = false;
    size_t listed = 0;

    for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
        put_file(names[i], "x");
    }
    add_directory(0, "folder");
    add_foreign_files(0);
    add_directory(1, "folder");
    add_foreign_files(1);

    /* Root 1 holds no File name, whatever else is in it. */
    zassert_equal(crux_file_dir_open(1, &dir), CRUX_FILE_OK);
    zassert_equal(crux_file_dir_read(dir, name, sizeof(name), &info, &end), CRUX_FILE_OK);
    zassert_true(end, "root 1 listed %s", name);
    zassert_equal(crux_file_dir_close(&dir), CRUX_FILE_OK);

    zassert_equal(crux_file_dir_open(0, &dir), CRUX_FILE_OK);
    for (;;) {
        zassert_equal(crux_file_dir_read(dir, name, sizeof(name), &info, &end), CRUX_FILE_OK);
        if (end) {
            break;
        }
        listed++;
        for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
            seen[i] = seen[i] || strcmp(name, names[i]) == 0;
        }
    }
    zassert_equal(crux_file_dir_close(&dir), CRUX_FILE_OK);
    zassert_equal(listed, ARRAY_SIZE(names), "listed %zu names", listed);
    for (size_t i = 0; i < ARRAY_SIZE(names); i++) {
        zassert_true(seen[i], "%s was not listed", names[i]);
    }
}

ZTEST(file_reliance_file, test_an_unmounted_volume_is_not_ready)
{
    crux_file_info_t info;
    crux_file_dir_t *dir = NULL;

    zassert_ok(fs_unmount(&mnt));
    zassert_equal(crux_file_stat(1, "x", &info), CRUX_FILE_ERR_NOT_READY);
    zassert_equal(crux_file_rename(1, "x", "y"), CRUX_FILE_ERR_NOT_READY);
    zassert_equal(crux_file_dir_open(1, &dir), CRUX_FILE_ERR_NOT_READY);
    zassert_is_null(dir);
    zassert_ok(fs_mount(&mnt));
}

ZTEST(file_reliance_file, test_names_are_stored_unencoded)
{
    struct fs_dirent entry;

    put_file("plain-name_1.txt", "abc");
    zassert_ok(fs_stat("/MMC:/a/plain-name_1.txt", &entry));
    zassert_equal(entry.type, FS_DIR_ENTRY_FILE);
    zassert_equal(entry.size, 3);
    zassert_equal(strcmp(entry.name, "plain-name_1.txt"), 0, "stored as %s", entry.name);
}

ZTEST(file_reliance_file, test_access_after_a_critical_error_reports_access)
{
    crux_file_t *other = NULL;
    size_t count = 0;

    zassert_equal(crux_file_open("crit", CRUX_FILE_CREATE_REPLACE, &kept_file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(kept_file, "x", 1, &count), CRUX_FILE_OK);
    fault_arm(DISK_OP_WRITE, 0, 1, -EIO);
    zassert_equal(crux_file_sync(kept_file), CRUX_FILE_ERR_IO);
    zassert_equal(fault.count, 0U, "the commit must have reached the fault");
    fault_clear();

    /* The volume is read-only now, and every change says so as an access error. */
    zassert_equal(crux_file_write(kept_file, "y", 1, &count), CRUX_FILE_ERR_ACCESS);
    zassert_equal(count, 0);
    zassert_equal(crux_file_sync(kept_file), CRUX_FILE_ERR_ACCESS);
    zassert_equal(crux_file_open("late", CRUX_FILE_CREATE_REPLACE, &other), CRUX_FILE_ERR_ACCESS);
    zassert_is_null(other);
    zassert_equal(crux_file_remove("crit"), CRUX_FILE_ERR_INVALID_STATE, "still open");
    zassert_equal(crux_file_close(&kept_file), CRUX_FILE_ERR_ACCESS, "the unsaved write was lost");
    zassert_is_null(kept_file);
    zassert_equal(crux_file_remove("crit"), CRUX_FILE_ERR_ACCESS);

    /* Reading still works, and a remount restores writing. */
    zassert_equal(crux_file_open("crit", CRUX_FILE_READ, &kept_file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&kept_file), CRUX_FILE_OK);
    zassert_ok(fs_unmount(&mnt));
    zassert_ok(fs_mount(&mnt));
    put_file("late", "ok");
    expect_content("late", "ok");
}

#define PROBE_STACK_BYTES 4096
#define PROBE_THREADS (TASKS + 2)

struct probe;
/* What a thread does once the case lets it go; probe_operations unless a case sets another. */
typedef void (*probe_job_t)(struct probe *probe);
/* Results of a job other than probe_operations, in the job's own order. */
#define PROBE_RESULTS 20

/* What a thread found when it used the file system, recorded for the case to judge: a failed
 * assertion in a helper thread would not fail the case. */
struct probe {
    struct k_thread thread;
    bool started;
    /* The thread was let go, so it may be inside the file system and must be waited for. */
    bool released;
    probe_job_t job;
    int results[PROBE_RESULTS];
    struct k_sem ready;
    struct k_sem go;
    struct k_sem done;
    uint32_t id;
    bool ran;
    int vfs_stat;
    int vfs_open;
    int vfs_opendir;
    int vfs_readdir;
    int vfs_close;
    crux_file_result_t file_stat;
    crux_file_result_t file_open;
    crux_file_result_t file_dir_read;
};

/* Directories opened by the case's own thread, for another thread to read or close. */
static struct fs_dir_t shared_dir;
/* Files opened by the case's own thread, for another thread to use or close. */
static struct fs_file_t shared_file;
static struct fs_file_t shared_second;
static crux_file_dir_t *shared_file_dir;
static crux_file_t *shared_crux_file;

static void probe_operations(struct probe *probe)
{
    struct fs_dirent entry;
    struct fs_file_t opened;
    struct fs_dir_t own_dir;
    crux_file_info_t info;
    crux_file_t *file = NULL;
    char name[CRUX_FILE_NAME_MAX + 1];
    bool end;

    probe->vfs_stat = fs_stat("/MMC:/a/probe-file", &entry);
    fs_file_t_init(&opened);
    probe->vfs_open = fs_open(&opened, "/MMC:/a/probe-file", FS_O_READ);
    if (probe->vfs_open == 0) {
        (void)fs_close(&opened);
    }
    fs_dir_t_init(&own_dir);
    probe->vfs_opendir = fs_opendir(&own_dir, "/MMC:/a");
    if (probe->vfs_opendir == 0) {
        (void)fs_closedir(&own_dir);
    }
    /* A directory that another thread opened. */
    probe->vfs_readdir = fs_readdir(&shared_dir, &entry);
    /* Only the thread that was refused tries to close it; the others leave it to this thread. */
    probe->vfs_close = (probe->id > TASKS) ? fs_close(&shared_file) : 0;
    probe->file_stat = crux_file_stat(0, "probe-file", &info);
    probe->file_open = crux_file_open("probe-file", CRUX_FILE_READ, &file);
    if (probe->file_open == CRUX_FILE_OK) {
        (void)crux_file_close(&file);
    }
    probe->file_dir_read = crux_file_dir_read(shared_file_dir, name, sizeof(name), &info, &end);
    probe->ran = true;
}

static void probe_entry(void *p1, void *p2, void *p3)
{
    struct probe *probe = p1;

    ARG_UNUSED(p2);
    ARG_UNUSED(p3);
    /* Asking for the identifier takes a slot, with no file system call, so that the table can be
     * filled while Reliance Edge's own table is not. */
    probe->id = RedOsTaskId();
    k_sem_give(&probe->ready);
    k_sem_take(&probe->go, K_FOREVER);
    if (probe->job != NULL) {
        probe->job(probe);
    } else {
        probe_operations(probe);
    }
    k_sem_give(&probe->done);
}

K_THREAD_STACK_ARRAY_DEFINE(probe_stacks, PROBE_THREADS, PROBE_STACK_BYTES);
static struct probe probes[PROBE_THREADS];

/* Each thread asks for its identifier. A thread that used the file system keeps its slot after it
 * has exited, and several threads that shared one identifier would still all work, so only the
 * identifiers themselves can show that they are different. The thread objects are different too,
 * because the identifier follows the object. */
#define ID_THREADS 3
K_THREAD_STACK_ARRAY_DEFINE(id_stacks, ID_THREADS, PROBE_STACK_BYTES);
static struct probe id_probes[ID_THREADS];

static void probe_start_on(struct probe *probe, k_thread_stack_t *stack, size_t stack_size)
{
    probe->id = 0;
    probe->ran = false;
    probe->released = false;
    probe->job = NULL;
    memset(probe->results, 0, sizeof(probe->results));
    probe->started = true;
    k_sem_init(&probe->ready, 0, 1);
    k_sem_init(&probe->go, 0, 1);
    k_sem_init(&probe->done, 0, 1);
    (void)k_thread_create(&probe->thread, stack, stack_size, probe_entry, probe, NULL, NULL,
                          K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
    zassert_ok(k_sem_take(&probe->ready, K_SECONDS(10)), "a thread did not start");
}

static void probe_start(struct probe *probe, size_t index)
{
    probe_start_on(probe, probe_stacks[index], K_THREAD_STACK_SIZEOF(probe_stacks[index]));
}

static void probe_release(struct probe *probe)
{
    probe->released = true;
    k_sem_give(&probe->go);
}

static void probe_finish(struct probe *probe)
{
    zassert_ok(k_sem_take(&probe->done, K_SECONDS(40)), "a thread did not finish");
    zassert_ok(k_thread_join(&probe->thread, K_SECONDS(10)));
    probe->started = false;
}

static void probe_run(struct probe *probe)
{
    probe_release(probe);
    probe_finish(probe);
}

/* Stop the threads of a case that failed half way. A thread that was let go may be inside the file
 * system, holding its locks, so it is let finish first; the stopped write lets it go after a
 * bounded wait, and this gives the release too. */
static void probes_stop(struct probe *set, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (!set[i].started) {
            continue;
        }
        if (set[i].released) {
            (void)k_thread_join(&set[i].thread, K_SECONDS(40));
        }
        k_thread_abort(&set[i].thread);
        set[i].started = false;
    }
}

/* After a case that failed half way: stop its threads and close what it opened. */
static void thread_cases_cleanup(void)
{
    hold.thread = NULL;
    asked_for = NULL;
    k_sem_give(&hold.release);
    probes_stop(probes, PROBE_THREADS);
    probes_stop(id_probes, ID_THREADS);
    memset(watched, 0, sizeof(watched));
    if (shared_file.mp != NULL) {
        (void)fs_close(&shared_file);
        fs_file_t_init(&shared_file);
    }
    if (shared_second.mp != NULL) {
        (void)fs_close(&shared_second);
        fs_file_t_init(&shared_second);
    }
    if (shared_dir.mp != NULL) {
        (void)fs_closedir(&shared_dir);
        fs_dir_t_init(&shared_dir);
    }
    if (shared_file_dir != NULL) {
        (void)crux_file_dir_close(&shared_file_dir);
    }
    if (shared_crux_file != NULL) {
        (void)crux_file_close(&shared_crux_file);
    }
}

ZTEST(file_reliance_file, test_task_ids_are_unique_across_threads)
{
    enum { COUNT = ID_THREADS };
    uint32_t ids[COUNT + 1];

    ids[COUNT] = RedOsTaskId();
    for (size_t i = 0; i < COUNT; ++i) {
        struct probe *probe = &id_probes[i];

        k_sem_init(&probe->ready, 0, 1);
        k_sem_init(&probe->go, 0, 1);
        k_sem_init(&probe->done, 0, 1);
        probe->id = 0;
        (void)k_thread_create(&probe->thread, id_stacks[i], K_THREAD_STACK_SIZEOF(id_stacks[i]),
                              probe_entry, probe, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
        zassert_ok(k_sem_take(&probe->ready, K_SECONDS(10)));
        ids[i] = probe->id;
        /* The thread has only asked for its identifier. Let it finish without further calls. */
        k_thread_abort(&probe->thread);
    }
    for (size_t i = 0; i < ARRAY_SIZE(ids); ++i) {
        zassert_not_equal(ids[i], 0U, "identifier %zu is zero", i);
        for (size_t j = i + 1U; j < ARRAY_SIZE(ids); ++j) {
            zassert_not_equal(ids[i], ids[j], "threads %zu and %zu share identifier %u", i, j,
                              (unsigned int)ids[i]);
        }
    }
}

/* Start threads until one finds every slot taken, and return that one. The threads before it have
 * slots and wait to be let go. A probe object keeps its slot from one case to the next, because the
 * slot follows the thread object. */
static struct probe *probes_until_refused(size_t *holders)
{
    struct probe *refused = NULL;

    *holders = 0;
    for (size_t i = 0; i < PROBE_THREADS && refused == NULL; ++i) {
        probe_start(&probes[i], i);
        if (probes[i].id > TASKS) {
            refused = &probes[i];
        } else {
            (*holders)++;
        }
    }
    zassert_not_null(refused, "no thread was refused within %d threads", PROBE_THREADS);
    return refused;
}

/*
 * Threads take slots until the table is full, and the next one is refused by the adapter before it
 * reaches Reliance Edge. The refused thread reads a directory that this thread opened, which must
 * be refused too. The threads that already had a slot then still work, which they would not if
 * the refused one had taken a registration of Reliance Edge's own table with an identifier other
 * than the spare one: the threads below ask for an identifier and make no file system call, so
 * that table has room until they do. The refused thread may close a file that this thread opened,
 * through the spare identifier. Threads that exit keep their slots, so the number of threads that
 * fit depends on the cases that ran before; at least two must remain.
 */
ZTEST(file_reliance_file, test_thread_limit_reports_no_resources)
{
    struct fs_dirent entry;
    size_t holders = 0;
    struct probe *refused = NULL;
    crux_file_info_t info;

    put_file("probe-file", "p");
    fs_dir_t_init(&shared_dir);
    zassert_ok(fs_opendir(&shared_dir, "/MMC:/a"));
    fs_file_t_init(&shared_file);
    zassert_ok(fs_open(&shared_file, "/MMC:/a/probe-file", FS_O_READ));
    shared_file_dir = NULL;
    zassert_equal(crux_file_dir_open(0, &shared_file_dir), CRUX_FILE_OK);

    refused = probes_until_refused(&holders);
    zassert_true(holders >= 2U, "only %zu threads fitted: raise the thread count", holders);

    probe_run(refused);
    zassert_true(refused->ran);
    zassert_equal(refused->vfs_stat, -ENOMEM);
    zassert_equal(refused->vfs_open, -ENOMEM);
    zassert_equal(refused->vfs_opendir, -ENOMEM);
    zassert_equal(refused->vfs_readdir, -ENOMEM, "a directory opened by another thread");
    zassert_ok(refused->vfs_close, "a file opened by another thread");
    zassert_equal(refused->file_stat, CRUX_FILE_ERR_NO_RESOURCES);
    zassert_equal(refused->file_open, CRUX_FILE_ERR_NO_RESOURCES);
    zassert_equal(refused->file_dir_read, CRUX_FILE_ERR_NO_RESOURCES);

    /* The threads with slots work, and so does this one. */
    for (size_t i = 0; i < holders; ++i) {
        probe_run(&probes[i]);
        zassert_true(probes[i].ran);
        zassert_ok(probes[i].vfs_stat, "thread %zu", i);
        zassert_ok(probes[i].vfs_open, "thread %zu", i);
        zassert_ok(probes[i].vfs_opendir, "thread %zu", i);
        zassert_ok(probes[i].vfs_readdir, "thread %zu", i);
        zassert_equal(probes[i].file_stat, CRUX_FILE_OK, "thread %zu", i);
        zassert_equal(probes[i].file_open, CRUX_FILE_OK, "thread %zu", i);
        zassert_equal(probes[i].file_dir_read, CRUX_FILE_OK, "thread %zu", i);
    }
    zassert_ok(fs_stat("/MMC:/a/probe-file", &entry));
    zassert_equal(crux_file_stat(0, "probe-file", &info), CRUX_FILE_OK);
    /* The refused thread closed the file. */
    zassert_is_null(shared_file.mp);
    zassert_ok(fs_closedir(&shared_dir));
    zassert_equal(crux_file_dir_close(&shared_file_dir), CRUX_FILE_OK);
}

/* Every thread with a slot makes one file system call, so that Reliance Edge has registered every
 * slot's identifier and its table has only the spare entry left. That includes the threads of the
 * identifier case, which only asked for theirs: a thread on the same object has the same slot. */
static void job_register(struct probe *probe)
{
    struct fs_dirent entry;

    probe->results[0] = fs_stat("/MMC:/a", &entry);
}

static void register_every_slot(size_t holders)
{
    for (size_t i = 0; i < ID_THREADS; ++i) {
        probe_start_on(&id_probes[i], id_stacks[i], K_THREAD_STACK_SIZEOF(id_stacks[i]));
        id_probes[i].job = job_register;
        probe_run(&id_probes[i]);
        /* An object that never had a slot has nothing to register. */
        if (id_probes[i].id <= TASKS) {
            zassert_ok(id_probes[i].results[0], "identifier thread %zu", i);
        }
    }
    for (size_t i = 0; i < holders; ++i) {
        probes[i].job = job_register;
        probe_run(&probes[i]);
        zassert_ok(probes[i].results[0], "thread %zu", i);
    }
}

enum { CLOSE_VFS_FILE, CLOSE_VFS_DIR, CLOSE_FILE, CLOSE_FILE_DIR, CLOSE_FILE_NULL };

static void job_close_shared(struct probe *probe)
{
    probe->results[CLOSE_VFS_FILE] = fs_close(&shared_file);
    probe->results[CLOSE_VFS_DIR] = fs_closedir(&shared_dir);
    probe->results[CLOSE_FILE] = (int)crux_file_close(&shared_crux_file);
    probe->results[CLOSE_FILE_DIR] = (int)crux_file_dir_close(&shared_file_dir);
    probe->results[CLOSE_FILE_NULL] = shared_crux_file == NULL && shared_file_dir == NULL;
}

/*
 * A thread without a slot closes files and directories that a thread with a slot opened, through
 * the VFS and through File. Every slot is registered with Reliance Edge first, so the closes need
 * the spare entry. With every handle closed, the volume unmounts and mounts again, and the writes
 * that the closes committed are there.
 */
ZTEST(file_reliance_file, test_thread_without_a_slot_can_close_what_another_thread_opened)
{
    size_t holders = 0;
    size_t count = 0;

    fs_file_t_init(&shared_file);
    zassert_ok(fs_open(&shared_file, "/MMC:/a/closed-elsewhere", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&shared_file, "v", 1), 1);
    fs_dir_t_init(&shared_dir);
    zassert_ok(fs_opendir(&shared_dir, "/MMC:/a"));
    zassert_equal(
        crux_file_open("file-closed-elsewhere", CRUX_FILE_CREATE_REPLACE, &shared_crux_file),
        CRUX_FILE_OK);
    zassert_equal(crux_file_write(shared_crux_file, "f", 1, &count), CRUX_FILE_OK);
    zassert_equal(crux_file_dir_open(1, &shared_file_dir), CRUX_FILE_OK);

    struct probe *refused = probes_until_refused(&holders);
    register_every_slot(holders);
    refused->job = job_close_shared;
    asked_count = 0;
    asked_for = &refused->thread;
    probe_run(refused);
    asked_for = NULL;
    /* The closes reached Reliance Edge, which also shows that the count used by the refusal case
     * sees Reliance Edge's requests. */
    zassert_true(asked_count > 0U, "Reliance Edge was never asked about the closing thread");

    zassert_ok(refused->results[CLOSE_VFS_FILE], "VFS close");
    zassert_ok(refused->results[CLOSE_VFS_DIR], "VFS closedir");
    zassert_equal(refused->results[CLOSE_FILE], CRUX_FILE_OK, "File close");
    zassert_equal(refused->results[CLOSE_FILE_DIR], CRUX_FILE_OK, "File listing close");
    zassert_true(refused->results[CLOSE_FILE_NULL], "File clears the handles");
    zassert_is_null(shared_file.mp);
    zassert_is_null(shared_dir.mp);

    zassert_ok(fs_unmount(&mnt), "a handle is still open");
    zassert_ok(fs_mount(&mnt));
    expect_content("file-closed-elsewhere", "f");
    fs_file_t_init(&helper);
    zassert_ok(fs_open(&helper, "/MMC:/a/closed-elsewhere", FS_O_READ));
    char value = 0;
    zassert_equal(fs_read(&helper, &value, 1), 1);
    zassert_equal(value, 'v');
    zassert_ok(fs_close(&helper));
}

static void job_close_first(struct probe *probe)
{
    probe->results[0] = fs_close(&shared_file);
}

static void job_close_second(struct probe *probe)
{
    probe->results[0] = fs_close(&shared_second);
}

/* The identifier of Reliance Edge's spare entry: one past the adapter's slots. */
#define SPARE_ID ((uint32_t)TASKS + 1U)

/*
 * Two threads without a slot close at the same time. The first is stopped inside its commit, while
 * it holds the spare identifier, and the second is let go then. Reliance Edge's own mutex alone
 * would hold the second back from the file system, but not from the adapter's record of who holds
 * the spare identifier: the first must still have it when it goes on. Both closes succeed.
 *
 * What this shows: the second thread does not take the identifier from the first, and nothing it
 * does reaches the disk before the first has finished. It does not show which lock the second
 * waits on, and the second's own commit may find nothing left to write, because the first commit
 * saves the whole volume. native_sim runs one thread at a time, so true simultaneity is not tested.
 */
ZTEST(file_reliance_file, test_thread_without_a_slot_closes_one_at_a_time)
{
    size_t holders = 0;

    fs_file_t_init(&shared_file);
    zassert_ok(fs_open(&shared_file, "/MMC:/a/first", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&shared_file, "1", 1), 1);
    fs_file_t_init(&shared_second);
    zassert_ok(fs_open(&shared_second, "/MMC:/a/second", FS_O_CREATE | FS_O_WRITE));
    zassert_equal(fs_write(&shared_second, "2", 1), 1);

    struct probe *first = probes_until_refused(&holders);
    size_t next = (size_t)(first - probes) + 1U;
    zassert_true(next < PROBE_THREADS);
    struct probe *second = &probes[next];
    probe_start(second, next);
    zassert_true(second->id > TASKS, "the second thread has a slot");
    /* The threads with slots are not needed here; let them finish. */
    for (size_t i = 0; i < holders; ++i) {
        probes[i].job = job_register;
        probe_run(&probes[i]);
    }

    first->job = job_close_first;
    second->job = job_close_second;
    k_sem_init(&hold.entered, 0, 1);
    k_sem_init(&hold.release, 0, 1);
    hold.id_after_release = 0;
    watch_count = 0;
    watched[0] = &first->thread;
    watched[1] = &second->thread;
    hold.thread = &first->thread;

    probe_release(first);
    int entered = k_sem_take(&hold.entered, K_SECONDS(10));
    probe_release(second);
    k_sleep(K_MSEC(200));
    size_t log_while_held = watch_count;
    bool second_done_early = k_sem_count_get(&second->done) != 0U;
    hold.thread = NULL;
    k_sem_give(&hold.release);
    probe_finish(first);
    probe_finish(second);
    memset(watched, 0, sizeof(watched));

    zassert_ok(entered, "the first close did not reach a block write");
    zassert_false(second_done_early, "the second close finished while the first was stopped");
    zassert_equal(log_while_held, 0U, "a block write ran while the first was stopped");
    zassert_equal(hold.id_after_release, SPARE_ID,
                  "the first thread lost the spare identifier: it has %u",
                  (unsigned int)hold.id_after_release);
    zassert_ok(first->results[0], "first close");
    zassert_ok(second->results[0], "second close");
    zassert_true(watch_count > 0U);
    bool second_seen = false;
    for (size_t i = 0; i < watch_count; ++i) {
        zassert_equal(watch_log[i].id, SPARE_ID, "write %zu by thread %zu had id %u", i,
                      watch_log[i].thread, (unsigned int)watch_log[i].id);
        if (watch_log[i].thread == 1U) {
            second_seen = true;
        } else {
            zassert_false(second_seen, "the first wrote after the second had started writing");
        }
    }
    expect_file("/MMC:/a/first", "1");
    expect_file("/MMC:/a/second", "2");
}

/* Every operation of the adapter except close and closedir, from a thread without a slot. */
enum {
    REFUSED_OPEN,
    REFUSED_READ,
    REFUSED_WRITE,
    REFUSED_LSEEK,
    REFUSED_TELL,
    REFUSED_TRUNCATE,
    REFUSED_SYNC,
    REFUSED_OPENDIR,
    REFUSED_READDIR,
    REFUSED_MOUNT,
    REFUSED_UNMOUNT,
    REFUSED_UNLINK,
    REFUSED_RENAME,
    REFUSED_MKDIR,
    REFUSED_STAT,
    REFUSED_STATVFS,
    REFUSED_MKFS,
    REFUSED_COUNT
};
BUILD_ASSERT(REFUSED_COUNT <= PROBE_RESULTS);

static const char *const refused_names[REFUSED_COUNT] = {
    "open",  "read",    "write",  "lseek",  "tell",  "truncate", "sync",    "opendir", "readdir",
    "mount", "unmount", "unlink", "rename", "mkdir", "stat",     "statvfs", "mkfs",
};

static void job_refused_calls(struct probe *probe)
{
    struct fs_file_t opened;
    struct fs_dir_t listing;
    struct fs_dirent entry;
    struct fs_statvfs volume;
    char byte;
    int *r = probe->results;

    fs_file_t_init(&opened);
    r[REFUSED_OPEN] = fs_open(&opened, "/MMC:/a/refused", FS_O_READ);
    r[REFUSED_READ] = (int)fs_read(&shared_file, &byte, 1);
    r[REFUSED_WRITE] = (int)fs_write(&shared_file, "z", 1);
    r[REFUSED_LSEEK] = fs_seek(&shared_file, 0, FS_SEEK_SET);
    r[REFUSED_TELL] = (int)fs_tell(&shared_file);
    r[REFUSED_TRUNCATE] = fs_truncate(&shared_file, 0);
    r[REFUSED_SYNC] = fs_sync(&shared_file);
    fs_dir_t_init(&listing);
    r[REFUSED_OPENDIR] = fs_opendir(&listing, "/MMC:/a");
    r[REFUSED_READDIR] = fs_readdir(&shared_dir, &entry);
    r[REFUSED_MOUNT] = fs_mount(&other);
    r[REFUSED_UNMOUNT] = fs_unmount(&mnt);
    r[REFUSED_UNLINK] = fs_unlink("/MMC:/a/refused");
    r[REFUSED_RENAME] = fs_rename("/MMC:/a/refused", "/MMC:/a/refused-moved");
    r[REFUSED_MKDIR] = fs_mkdir("/MMC:/a/refused-dir");
    r[REFUSED_STAT] = fs_stat("/MMC:/a/refused", &entry);
    r[REFUSED_STATVFS] = fs_statvfs("/MMC:", &volume);
    r[REFUSED_MKFS] = fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)DISK, NULL, 0);
}

/* A thread without a slot is refused every other operation, with no effect: nothing is written,
 * the shared file keeps its position, length and name, and the volume stays mounted. */
ZTEST(file_reliance_file, test_thread_without_a_slot_is_refused_every_other_call)
{
    struct fs_dirent entry;
    size_t holders = 0;

    fs_file_t_init(&shared_file);
    zassert_ok(fs_open(&shared_file, "/MMC:/a/refused", FS_O_CREATE | FS_O_RDWR));
    zassert_equal(fs_write(&shared_file, "abc", 3), 3);
    zassert_ok(fs_sync(&shared_file));
    zassert_ok(fs_seek(&shared_file, 1, FS_SEEK_SET));
    fs_dir_t_init(&shared_dir);
    zassert_ok(fs_opendir(&shared_dir, "/MMC:/a"));

    struct probe *refused = probes_until_refused(&holders);
    refused->job = job_refused_calls;
    uint32_t writes_before = write_calls;
    asked_count = 0;
    asked_for = &refused->thread;
    probe_run(refused);
    asked_for = NULL;
    uint32_t writes_after = write_calls;

    for (size_t i = 0; i < REFUSED_COUNT; ++i) {
        zassert_equal(refused->results[i], -ENOMEM, "%s returned %d", refused_names[i],
                      refused->results[i]);
    }
    /* Reliance Edge itself refuses an identifier it has no room for, with the same error, so the
     * results alone cannot show where a call was refused. That it never asked for this thread's
     * identifier shows that no call reached it. */
    zassert_equal(asked_count, 0U, "Reliance Edge was called %u times for the refused thread",
                  (unsigned int)asked_count);
    zassert_equal(writes_after, writes_before, "a refused call wrote to the disk");
    zassert_equal(fs_tell(&shared_file), 1);
    zassert_ok(fs_seek(&shared_file, 0, FS_SEEK_END));
    zassert_equal(fs_tell(&shared_file), 3);
    zassert_ok(fs_close(&shared_file));
    zassert_ok(fs_closedir(&shared_dir));
    zassert_ok(fs_stat("/MMC:/a/refused", &entry));
    zassert_equal(fs_stat("/MMC:/a/refused-moved", &entry), -ENOENT);
    zassert_equal(fs_stat("/MMC:/a/refused-dir", &entry), -ENOENT);
    zassert_ok(fs_unlink("/MMC:/a/refused"));
}

/* Where two values of this profile sit in the first block of the volume, found by looking at a
 * medium that this profile formatted: the checksum of the block, and the base-2 logarithm of the
 * block size. The case checks both, so that a different layout fails it instead of passing it. */
#define MASTER_CRC_OFFSET 4U
#define MASTER_BLOCK_SIZE_P2_OFFSET 46U
#define BLOCK_SIZE_P2 12U

/* The checksum of a block as the file system stores it. */
uint32_t RedCrcNode(const void *pBuffer);

static void write_first_block(const uint8_t *block)
{
    zassert_ok(__real_disk_access_write(DISK, block, VOLUME_FIRST, BLOCK_BYTES / SECTOR_BYTES));
}

static void seal_first_block(uint8_t *block)
{
    uint32_t crc = RedCrcNode(block);

    block[MASTER_CRC_OFFSET + 0] = (uint8_t)crc;
    block[MASTER_CRC_OFFSET + 1] = (uint8_t)(crc >> 8);
    block[MASTER_CRC_OFFSET + 2] = (uint8_t)(crc >> 16);
    block[MASTER_CRC_OFFSET + 3] = (uint8_t)(crc >> 24);
}

/* A medium formatted with another block size is intact as far as its checksum goes, so a mount that
 * refuses it does so for the profile, and it must neither write nor format. */
ZTEST(file_reliance_file, test_mismatched_profile_mount_fails_and_digest_is_unchanged)
{
    static uint8_t block[BLOCK_BYTES];
    static uint8_t original[BLOCK_BYTES];

    put_file("kept", "kept");
    zassert_ok(fs_unmount(&mnt));
    zassert_ok(__real_disk_access_read(DISK, original, VOLUME_FIRST, BLOCK_BYTES / SECTOR_BYTES));
    memcpy(block, original, sizeof(block));
    zassert_equal(block[MASTER_BLOCK_SIZE_P2_OFFSET], BLOCK_SIZE_P2, "the layout is not as found");
    seal_first_block(block);
    zassert_mem_equal(block, original, sizeof(block), "the checksum is not computed as stored");

    /* Another block size, with a checksum that fits it. */
    block[MASTER_BLOCK_SIZE_P2_OFFSET] = BLOCK_SIZE_P2 + 1U;
    seal_first_block(block);
    zassert_true(memcmp(block, original, sizeof(block)) != 0);
    write_first_block(block);
    uint64_t digest = medium_digest();
    write_calls = 0;
    int rc = fs_mount(&mnt);
    TC_PRINT("a mount with another block size returned %d\n", rc);
    zassert_true(rc < 0 && rc != -EBUSY, "mount returned %d", rc);
    zassert_equal(write_calls, 0, "a failed mount must not write");
    zassert_equal(medium_digest(), digest, "a failed mount must not format");
    crux_file_t *file = NULL;
    zassert_equal(crux_file_open("kept", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);

    /* The original block mounts again. Its stored checksum equals the one computed above with the
     * same function, so the refusal above was the block size and not the checksum. */
    write_first_block(original);
    zassert_ok(fs_mount(&mnt));
    expect_content("kept", "kept");
}

ZTEST(file_reliance_file, test_writes_stay_inside_the_configured_sector_range)
{
    static uint8_t scan[SECTOR_BYTES];
    const uint32_t end = VOLUME_FIRST + VOLUME_SECTORS;

    zassert_ok(fs_unmount(&mnt));
    /* A pattern everywhere, so that a write outside the volume cannot hide in zeros. */
    medium_fill(0xC3);
    written_first = UINT32_MAX;
    written_last = 0;

    rebuild_volume();
    put_file("range", "inside");
    fill_volume("filler");
    zassert_equal(crux_file_remove("filler"), CRUX_FILE_OK);
    zassert_equal(crux_file_rename(0, "range", "moved"), CRUX_FILE_OK);
    zassert_ok(fs_unmount(&mnt));

    zassert_true(written_first <= written_last, "nothing was written");
    zassert_equal(written_first, VOLUME_FIRST, "the volume starts at sector %u, writes at %u",
                  (unsigned int)VOLUME_FIRST, (unsigned int)written_first);
    zassert_true(written_last < end, "a write reached sector %u, past the volume's end %u",
                 (unsigned int)written_last, (unsigned int)end);
    /* The volume filled up, so its last block was used. */
    zassert_true(written_last + BLOCK_BYTES / SECTOR_BYTES >= end, "the highest write is %u of %u",
                 (unsigned int)written_last, (unsigned int)end);
    for (uint32_t sector = 0; sector < DISK_SECTORS; ++sector) {
        if (sector - (uint32_t)VOLUME_FIRST < (uint32_t)VOLUME_SECTORS) {
            continue; /* inside the volume */
        }
        zassert_ok(__real_disk_access_read(DISK, scan, sector, 1));
        for (size_t i = 0; i < sizeof(scan); ++i) {
            zassert_equal(scan[i], 0xC3, "sector %u outside the volume was changed",
                          (unsigned int)sector);
        }
    }
    zassert_ok(fs_mount(&mnt));
}

ZTEST_SUITE(file_reliance_file, NULL, file_setup, file_before, file_after, file_teardown);
