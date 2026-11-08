/* SPDX-License-Identifier: Apache-2.0 */
/* The file system under test, mounted at POWERCUT_MOUNT_POINT on the simulator's disk "MMC". */
#ifndef CRUX_TESTS_EMMC_POWERCUT_FS_BACKEND_H
#define CRUX_TESTS_EMMC_POWERCUT_FS_BACKEND_H

#define POWERCUT_MOUNT_POINT "/MMC:"

int powercut_fs_format(void);
int powercut_fs_mount(void); /* never formats */
int powercut_fs_unmount(void);

#endif /* CRUX_TESTS_EMMC_POWERCUT_FS_BACKEND_H */
