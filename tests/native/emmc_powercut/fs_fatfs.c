/* SPDX-License-Identifier: Apache-2.0 */
/* FatFs as the file system under test, volume "MMC:". */
#include "fs_backend.h"

#include <ff.h>
#include <stdint.h>
#include <zephyr/fs/fs.h>

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = POWERCUT_MOUNT_POINT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

int powercut_fs_format(void)
{
    return fs_mkfs(FS_FATFS, (uintptr_t) "MMC:", NULL, 0);
}

int powercut_fs_mount(void)
{
    return fs_mount(&mount);
}

int powercut_fs_unmount(void)
{
    return fs_unmount(&mount);
}
