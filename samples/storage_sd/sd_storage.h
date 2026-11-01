/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* microSD composition for the File component: mount order and failure policy. Project code, not a
 * Crux API; it never formats a medium, creates a directory or retries. */

#define SD_STORAGE_DISK "SD"
#define SD_STORAGE_MOUNT "/SD:"
#define SD_STORAGE_FILE_DIR SD_STORAGE_MOUNT "/files"
#define SD_STORAGE_SETTINGS_DIR SD_STORAGE_MOUNT "/settings"

enum sd_storage_status {
    SD_STORAGE_READY = 0,
    SD_STORAGE_NO_MEDIA,       /* the disk did not initialise */
    SD_STORAGE_MOUNT_FAILED,   /* no FAT volume; never formatted */
    SD_STORAGE_LAYOUT_MISSING, /* a required directory is missing; never created */
    SD_STORAGE_BIND_FAILED,
    SD_STORAGE_ALREADY_STARTED,
};

/* Disk init, mount without format, layout check, then File root binding. A failure unmounts the
 * volume (the unmount result is not checked) and leaves the File component unbound, so a later
 * call may succeed. */
enum sd_storage_status sd_storage_start(void);

/* Application-owned unmount and remount, valid only after a successful start. The File provider
 * stays bound to the same root; open handles must be closed first because FatFs does not refuse an
 * unmount while one is open. stop is not a power-down: the disk stays initialised. After a stop,
 * start reports ALREADY_STARTED and remount is the way back. A card swap needs a reboot.
 * Return 0 on success; -EINVAL out of order (stop when not mounted, remount before start),
 * -EALREADY for a remount of a mounted volume, else an errno. */
int sd_storage_stop(void);
int sd_storage_remount(void);
