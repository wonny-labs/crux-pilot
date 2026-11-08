/* SPDX-License-Identifier: Apache-2.0 */
#include "format.h"
#include "emmc_storage.h"
#include "fs_reliance_edge.h"
#include <stdint.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

/* The mount of emmc_storage.c: the same file system type, mount point and disk name (the macros of
 * emmc_storage.h). That file keeps its definition private, so it is repeated here. */
static struct fs_mount_t mount = {
    .type = CRUX_FS_RELIANCE_EDGE,
    .mnt_point = EMMC_STORAGE_MOUNT,
    .storage_dev = (void *)EMMC_STORAGE_DISK,
};

enum reliance_format_status reliance_format_once(void)
{
    if (disk_access_init(EMMC_STORAGE_DISK) != 0) {
        return RELIANCE_FORMAT_NO_MEDIA;
    }
    /* Nothing is formatted or created before this check. A mount never formats. It fails on a card
     * without a volume, and it can also fail on a card that holds one (an I/O error, damage, or a
     * different sector range in this build); that card is formatted below. */
    if (fs_mount(&mount) == 0) {
        (void)fs_unmount(&mount);
        return RELIANCE_FORMAT_VOLUME_PRESENT;
    }
    if (fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)EMMC_STORAGE_DISK, NULL, 0) != 0) {
        return RELIANCE_FORMAT_MKFS_FAILED;
    }
    if (fs_mount(&mount) != 0) {
        return RELIANCE_FORMAT_MOUNT_FAILED;
    }
    enum reliance_format_status status = RELIANCE_FORMAT_OK;
    if (fs_mkdir(EMMC_STORAGE_FILE_DIR) != 0 || fs_mkdir(EMMC_STORAGE_SETTINGS_DIR) != 0) {
        status = RELIANCE_FORMAT_MKDIR_FAILED;
    }
    if (fs_unmount(&mount) != 0) {
        status = RELIANCE_FORMAT_MKDIR_FAILED;
    }
    return status;
}

/* The main of the format image: one call, one console line, then nothing. Do not flash this image
 * until the physical gate has been approved for the current task; it formats the card. */
int main(void)
{
    enum reliance_format_status status = reliance_format_once();
    printk("reliance_format: %d\n", (int)status);
    return 0;
}
