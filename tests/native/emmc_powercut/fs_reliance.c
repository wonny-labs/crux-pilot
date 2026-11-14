/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Reliance Edge as the file system under test, through the Crux VFS adapter, on disk "MMC". The
 * adapter does not initialise the disk, so this file does, once per process, as a composition
 * would. A mount never formats.
 */
#include "fs_backend.h"

#include <fs_reliance_edge.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>

#define POWERCUT_DISK "MMC"

static struct fs_mount_t mount = {
    .type = CRUX_FS_RELIANCE_EDGE,
    .mnt_point = POWERCUT_MOUNT_POINT,
    .storage_dev = (void *)POWERCUT_DISK,
};

/* The disk driver initialises the card again on each call, so it is called only once. */
static int disk_ready(void)
{
    static bool ready;
    int rc;

    if (ready) {
        return 0;
    }
    rc = disk_access_init(POWERCUT_DISK);
    ready = rc == 0;
    return rc;
}

int powercut_fs_format(void)
{
    int rc = disk_ready();

    return rc != 0 ? rc : fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)POWERCUT_DISK, NULL, 0);
}

int powercut_fs_mount(void)
{
    int rc = disk_ready();

    return rc != 0 ? rc : fs_mount(&mount);
}

int powercut_fs_unmount(void)
{
    return fs_unmount(&mount);
}
