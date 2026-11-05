/* SPDX-License-Identifier: Apache-2.0 */
/* Internal to the adapter: shared between the VFS operations and the block device service. */
#ifndef CRUX_FS_RELIANCE_EDGE_PRIV_H
#define CRUX_FS_RELIANCE_EDGE_PRIV_H

/* Names the disk that the block device service uses. Called by mount and mkfs, while no volume is
 * mounted; the string must outlive the mount. */
void crux_red_bdev_select(const char *disk_name);

#endif /* CRUX_FS_RELIANCE_EDGE_PRIV_H */
