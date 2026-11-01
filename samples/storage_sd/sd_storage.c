/* SPDX-License-Identifier: Apache-2.0 */
#include "sd_storage.h"
#include "file_native.h"
#include <crux/file.h>
#include <errno.h>
#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = SD_STORAGE_MOUNT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static bool bound;

static bool is_directory(const char *path)
{
    struct fs_dirent entry;
    return fs_stat(path, &entry) == 0 && entry.type == FS_DIR_ENTRY_DIR;
}

static enum sd_storage_status mount_and_check(void)
{
    if (disk_access_init(SD_STORAGE_DISK) != 0) {
        return SD_STORAGE_NO_MEDIA;
    }
    if (fs_mount(&mount) != 0) {
        return SD_STORAGE_MOUNT_FAILED;
    }
    if (!is_directory(SD_STORAGE_FILE_DIR) || !is_directory(SD_STORAGE_SETTINGS_DIR)) {
        (void)fs_unmount(&mount);
        return SD_STORAGE_LAYOUT_MISSING;
    }
    return SD_STORAGE_READY;
}

enum sd_storage_status sd_storage_start(void)
{
    if (bound) {
        return SD_STORAGE_ALREADY_STARTED;
    }
    enum sd_storage_status status = mount_and_check();
    if (status != SD_STORAGE_READY) {
        return status;
    }
    if (crux_file_zephyr_bind(SD_STORAGE_FILE_DIR) != CRUX_FILE_OK) {
        (void)fs_unmount(&mount);
        return SD_STORAGE_BIND_FAILED;
    }
    bound = true;
    return SD_STORAGE_READY;
}

int sd_storage_stop(void)
{
    return fs_unmount(&mount);
}

int sd_storage_remount(void)
{
    return mount_and_check() == SD_STORAGE_READY ? 0 : -EIO;
}
