/* SPDX-License-Identifier: Apache-2.0 */
#include "emmc_storage.h"
#include "fs_reliance_edge.h"
#include <crux/file.h>
#include <crux/file_zephyr.h>
#include <errno.h>
#include <stdbool.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>

/* The adapter does not initialise the disk and a mount never formats, so nothing below can write a
 * new volume. fs_data is unused by the adapter. */
static struct fs_mount_t mount = {
    .type = CRUX_FS_RELIANCE_EDGE,
    .mnt_point = EMMC_STORAGE_MOUNT,
    .storage_dev = (void *)EMMC_STORAGE_DISK,
};
static bool bound;
static bool mounted;

static bool is_directory(const char *path)
{
    struct fs_dirent entry;
    return fs_stat(path, &entry) == 0 && entry.type == FS_DIR_ENTRY_DIR;
}

static enum emmc_storage_status mount_and_check(void)
{
    if (disk_access_init(EMMC_STORAGE_DISK) != 0) {
        return EMMC_STORAGE_NO_MEDIA;
    }
    if (fs_mount(&mount) != 0) {
        return EMMC_STORAGE_MOUNT_FAILED;
    }
    if (!is_directory(EMMC_STORAGE_FILE_DIR) || !is_directory(EMMC_STORAGE_SETTINGS_DIR)) {
        (void)fs_unmount(&mount);
        return EMMC_STORAGE_LAYOUT_MISSING;
    }
    mounted = true;
    return EMMC_STORAGE_READY;
}

enum emmc_storage_status emmc_storage_start(void)
{
    if (bound) {
        return EMMC_STORAGE_ALREADY_STARTED;
    }
    enum emmc_storage_status status = mount_and_check();
    if (status != EMMC_STORAGE_READY) {
        return status;
    }
    if (crux_file_zephyr_bind(EMMC_STORAGE_FILE_DIR) != CRUX_FILE_OK) {
        (void)fs_unmount(&mount);
        mounted = false;
        return EMMC_STORAGE_BIND_FAILED;
    }
    bound = true;
    return EMMC_STORAGE_READY;
}

int emmc_storage_stop(void)
{
    if (!mounted) {
        return -EINVAL;
    }
    int rc = fs_unmount(&mount);
    if (rc == 0) {
        mounted = false;
    }
    return rc;
}

int emmc_storage_remount(void)
{
    if (!bound) {
        return -EINVAL; /* start first: a mount without a bound root would block a later start */
    }
    if (mounted) {
        return -EALREADY;
    }
    return mount_and_check() == EMMC_STORAGE_READY ? 0 : -EIO;
}
