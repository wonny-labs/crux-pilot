/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Private composition header for the Reliance Edge file system in the Zephyr VFS. It is not a
 * crux/ API. Mount with this type, `storage_dev` set to the disk_access name (for example "MMC")
 * and `fs_data` unused. A mount never formats. With CONFIG_CRUX_FS_RELIANCE_EDGE_FORMAT,
 * fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)"MMC", NULL, 0) formats that disk.
 */
#ifndef CRUX_FS_RELIANCE_EDGE_H
#define CRUX_FS_RELIANCE_EDGE_H

#include <zephyr/fs/fs.h>

#define CRUX_FS_RELIANCE_EDGE FS_TYPE_EXTERNAL_BASE

#endif /* CRUX_FS_RELIANCE_EDGE_H */
