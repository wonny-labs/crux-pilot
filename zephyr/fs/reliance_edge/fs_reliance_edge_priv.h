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

/* For close and closedir only. Gives the calling thread a slot as crux_red_task_enter does and
 * returns false; or, when every slot belongs to another thread, waits until the thread holds the
 * spare identifier and returns true. A thread that gets true calls crux_red_spare_release after its
 * last Reliance Edge call of the operation, including reading red_errno, and holds no other lock of
 * the adapter while it waits here. */
bool crux_red_spare_take(void);
void crux_red_spare_release(void);

#endif /* CRUX_FS_RELIANCE_EDGE_PRIV_H */
