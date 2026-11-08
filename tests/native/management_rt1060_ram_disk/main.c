/*
 * The RAM disk of the RT1060-EVKB bench image (row C3, decision D2), on native_sim, with the real FatFs of the
 * bench image's configuration (long names on the stack, 127 characters, six open files, no format on mount).
 *
 * Question: does the disk of ../../zephyr/management_rt1060_compile/ram_disk.overlay hold the largest content
 * that the image's File service and credential store allow, with room to spare? The File service reserves bytes
 * against its area's limits, so the area can never hold more than its limit; the credential store writes two
 * slot files. The test builds that worst case on the real FatFs, in the order that hurts most (the area full
 * first, the store's slots after it) and reports the free space it leaves. Nothing here touches a board or a card.
 */
#include "bench_limits.h"
#include "credential_port.h"

#include <ff.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

#define SECTOR_SIZE DT_PROP(DT_NODELABEL(bench_ram_disk), sector_size)
#define SECTOR_COUNT DT_PROP(DT_NODELABEL(bench_ram_disk), sector_count)
#define DISK_BYTES ((uint32_t)(SECTOR_SIZE * SECTOR_COUNT))

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/RAM:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static uint8_t chunk[2048];
static bool mounted;

static void format_and_mount(void)
{
    if (mounted) {
        zassert_ok(fs_unmount(&mount));
        mounted = false;
    }
    /* A RAM disk is blank at every boot, and what a blank disk answers a mount with is the bench's trigger. */
    memset(chunk, 0, sizeof(chunk));
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    mounted = true;
}

static int write_file(const char *path, uint32_t bytes)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int rc = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
    if (rc != 0) {
        return rc;
    }
    memset(chunk, (int)(bytes & 0xff), sizeof(chunk));
    for (uint32_t done = 0; done < bytes;) {
        uint32_t piece = MIN(bytes - done, (uint32_t)sizeof(chunk));
        ssize_t written = fs_write(&file, chunk, piece);
        if (written != (ssize_t)piece) {
            (void)fs_close(&file);
            return written < 0 ? (int)written : -ENOSPC;
        }
        done += piece;
    }
    return fs_close(&file);
}

typedef struct {
    uint32_t cluster_bytes, clusters, free_clusters;
} space_t;

static space_t space(void)
{
    struct fs_statvfs vfs;
    zassert_ok(fs_statvfs("/RAM:", &vfs));
    return (space_t){(uint32_t)vfs.f_frsize, (uint32_t)vfs.f_blocks, (uint32_t)vfs.f_bfree};
}

static void make_directories(void)
{
    zassert_ok(fs_mkdir("/RAM:/secure-a"));
    zassert_ok(fs_mkdir("/RAM:/secure-b"));
    zassert_ok(fs_mkdir("/RAM:/files"));
}

/* The store's slots, after the area is full: the order that leaves it the least room. */
static void write_store_slots(void)
{
    zassert_ok(write_file("/RAM:/secure-a/slot", CRUX_CREDENTIAL_SLOT_BYTES));
    zassert_ok(write_file("/RAM:/secure-b/slot", CRUX_CREDENTIAL_SLOT_BYTES));
}

static void report(const char *what, space_t before, space_t after)
{
    uint32_t free_bytes = after.free_clusters * after.cluster_bytes;
    printk("RAMDISK %s: disk %u B, cluster %u B, data clusters %u (%u B), free before %u B, free after %u B (%u %% of the disk)\n",
           what, DISK_BYTES, after.cluster_bytes, after.clusters, after.clusters * after.cluster_bytes,
           before.free_clusters * before.cluster_bytes, free_bytes,
           (unsigned)((uint64_t)free_bytes * 100u / DISK_BYTES));
}

ZTEST(ram_disk_size, test_geometry_is_the_one_of_the_overlay)
{
    uint32_t sectors = 0, size = 0;
    zassert_ok(disk_access_ioctl("RAM", DISK_IOCTL_GET_SECTOR_COUNT, &sectors));
    zassert_ok(disk_access_ioctl("RAM", DISK_IOCTL_GET_SECTOR_SIZE, &size));
    zassert_equal(sectors, SECTOR_COUNT);
    zassert_equal(size, SECTOR_SIZE);
    printk("RAMDISK geometry: %u sectors of %u B = %u B\n", sectors, size, sectors * size);
}

ZTEST(ram_disk_size, test_a_blank_ram_disk_is_not_formatted_by_the_mount)
{
    /* FS_MOUNT_FLAG_NO_FORMAT and no automatic format: a blank disk is a failed mount. The bench's decision
     * (src/bench_medium.c) is what formats it, and only a RAM disk. */
    zassert_not_equal(fs_mount(&mount), 0, "a blank disk was mounted or formatted by the mount");
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    mounted = true;
    zassert_ok(fs_unmount(&mount));
    mounted = false;
    /* A second boot, in effect: the memory is zero again. The disk is formatted from nothing every time, so a file
     * that was there is not. */
}

ZTEST(ram_disk_size, test_the_area_at_its_limit_four_files_of_the_largest_size_and_the_store_fit)
{
    format_and_mount();
    space_t blank = space();
    make_directories();
    char path[40];
    for (unsigned i = 0; i < BENCH_AREA_MAX_BYTES / BENCH_AREA_MAX_FILE_BYTES; i++) {
        snprintf(path, sizeof(path), "/RAM:/files/%u.dat", i);
        zassert_ok(write_file(path, BENCH_AREA_MAX_FILE_BYTES), "file %u", i);
    }
    write_store_slots();
    report("area as 4 files of 64 KiB, then both slots", blank, space());
    zassert_true(space().free_clusters * space().cluster_bytes >=
                     BENCH_COMMIT_LEFTOVERS * space().cluster_bytes,
                 "not even the leftovers of a commit fit");
}

ZTEST(ram_disk_size, test_the_area_at_its_limit_with_the_most_entries_and_the_store_fit)
{
    format_and_mount();
    space_t blank = space();
    make_directories();
    /* Sixteen entries with names of 63 characters (the longest the File names allow: the most directory entries
     * per file), each one byte over a cluster boundary where the limit allows it: the most slack per file. */
    uint32_t each = BENCH_AREA_MAX_BYTES / BENCH_AREA_MAX_ENTRIES;
    uint32_t whole_clusters = (each - 1u) / blank.cluster_bytes;
    uint32_t size = whole_clusters * blank.cluster_bytes + 1u;
    zassert_true(size * BENCH_AREA_MAX_ENTRIES <= BENCH_AREA_MAX_BYTES);
    for (unsigned i = 0; i < BENCH_AREA_MAX_ENTRIES; i++) {
        char name[64];
        snprintf(name, sizeof(name), "%02u", i);
        memset(name + 2, 'a', 61);
        name[63] = '\0';
        char path[96];
        snprintf(path, sizeof(path), "/RAM:/files/%s", name);
        zassert_ok(write_file(path, size), "entry %u", i);
    }
    write_store_slots();
    report("area as 16 entries of 63-character names at the worst slack, then both slots", blank, space());
    zassert_true(space().free_clusters > BENCH_COMMIT_LEFTOVERS, "the leftovers of a commit do not fit");
}

ZTEST(ram_disk_size, test_one_more_file_than_the_disk_has_room_for_fails_cleanly)
{
    /* The File service reserves against its limits and the disk is larger than they sum to, so the disk is never
     * the first to refuse. If it were (a limit raised, a disk lowered), it would say so with ENOSPC and not hang. */
    format_and_mount();
    make_directories();
    int rc = 0;
    char path[40];
    for (unsigned i = 0; i < 64 && rc == 0; i++) {
        snprintf(path, sizeof(path), "/RAM:/files/fill%u", i);
        rc = write_file(path, 65536);
    }
    zassert_true(rc == -ENOSPC || rc == -ENOMEM || rc == -EIO || rc == -EFBIG, "rc %d", rc);
    printk("RAMDISK filled to refusal with rc %d\n", rc);
}

ZTEST_SUITE(ram_disk_size, NULL, NULL, NULL, NULL, NULL);
