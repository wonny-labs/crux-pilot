/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* eMMC composition for the File component: mount order and failure policy, with Reliance Edge as
 * the file system. Project code, not a Crux API; it never formats a medium, creates a directory or
 * retries. */

#define EMMC_STORAGE_DISK CONFIG_SAMPLE_EMMC_DISK_NAME
#define EMMC_STORAGE_MOUNT "/MMC:"
#define EMMC_STORAGE_FILE_DIR EMMC_STORAGE_MOUNT "/files"
#define EMMC_STORAGE_SETTINGS_DIR EMMC_STORAGE_MOUNT "/settings"

enum emmc_storage_status {
    EMMC_STORAGE_READY = 0,
    EMMC_STORAGE_NO_MEDIA,       /* the disk did not initialise */
    EMMC_STORAGE_MOUNT_FAILED,   /* no Reliance Edge volume; never formatted */
    EMMC_STORAGE_LAYOUT_MISSING, /* a required directory is missing; never created */
    EMMC_STORAGE_BIND_FAILED,
    EMMC_STORAGE_ALREADY_STARTED,
};

/* Disk init, mount without format, layout check, then File root binding. A failure unmounts the
 * volume (the unmount result is not checked) and leaves the File component unbound, so a later
 * call may succeed. */
enum emmc_storage_status emmc_storage_start(void);

/* Application-owned unmount and remount, valid only after a successful start. The File provider
 * stays bound to the same root. Reliance Edge refuses an unmount while a handle is open, so close
 * every handle first. stop is not a power-down: the disk stays initialised. After a stop, start
 * reports ALREADY_STARTED and remount is the way back. A card swap needs a reboot.
 * Return 0 on success; -EINVAL out of order (stop when not mounted, remount before start),
 * -EALREADY for a remount of a mounted volume, else an errno. */
int emmc_storage_stop(void);
int emmc_storage_remount(void);
