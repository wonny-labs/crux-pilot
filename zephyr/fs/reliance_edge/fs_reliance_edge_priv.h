/* SPDX-License-Identifier: Apache-2.0 */
/* Internal to the adapter: shared between the VFS operations and the block device service. */
#ifndef CRUX_FS_RELIANCE_EDGE_PRIV_H
#define CRUX_FS_RELIANCE_EDGE_PRIV_H

#include <stdbool.h>

/* Names the disk that the block device service uses. Called by mount and mkfs, while no volume is
 * mounted; the string must outlive the mount. */
void crux_red_bdev_select(const char *disk_name);

/* Gives the calling thread a task slot, or finds the one it has. False when all slots belong to
 * other threads: the adapter then refuses the call, and Reliance Edge is not called. */
bool crux_red_task_enter(void);

#endif /* CRUX_FS_RELIANCE_EDGE_PRIV_H */
