/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

/* The one-shot format of the Stage A card. Compiled only into the format image
 * (CONFIG_SAMPLE_RELIANCE_SD_FORMAT); no other image of this sample links it. */

enum reliance_format_status {
    RELIANCE_FORMAT_OK = 0,         /* formatted, /MMC:/files and /MMC:/settings created */
    RELIANCE_FORMAT_NO_MEDIA,       /* the disk did not initialise; nothing was written */
    RELIANCE_FORMAT_VOLUME_PRESENT, /* the volume mounted; nothing was formatted or created */
    RELIANCE_FORMAT_MKFS_FAILED,    /* the mount failed and the format then failed */
    RELIANCE_FORMAT_MOUNT_FAILED,   /* formatted, but the new volume did not mount */
    RELIANCE_FORMAT_MKDIR_FAILED,   /* a directory was not created, or the unmount that commits them
                                       failed */
};

/* Formats the card once, and only when no Reliance Edge volume mounts on it. The sequence is: disk
 * init, a mount attempt (success: unmount and return VOLUME_PRESENT), format, mount, create
 * /MMC:/files and /MMC:/settings, unmount. The format is reached only after the mount attempt has
 * failed, so a card that already holds a volume is never formatted. The volume stays unmounted on
 * return. */
enum reliance_format_status reliance_format_once(void);
