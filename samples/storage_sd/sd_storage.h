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

/* Disk init, mount without format, layout check, then File root binding. A failure leaves the
 * volume unmounted and the File component unbound, so a later call may succeed. */
enum sd_storage_status sd_storage_start(void);

/* Application-owned unmount and remount. The File provider stays bound to the same root; open
 * handles must be closed first because FatFs does not refuse an unmount while one is open. A card
 * swap needs a reboot. Return 0 on success, else a negative errno. */
int sd_storage_stop(void);
int sd_storage_remount(void);
