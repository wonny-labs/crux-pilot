/* SPDX-License-Identifier: Apache-2.0 */
/* The FatFs RAM disk of the Settings File suite: the fixture that main.c includes by default. */
#ifndef CRUX_TESTS_CONFIGURATION_SETTINGS_FIXTURE_FATFS_H
#define CRUX_TESTS_CONFIGURATION_SETTINGS_FIXTURE_FATFS_H

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

#define FIXTURE_ROOT "/RAM:"
#define FIXTURE_HAS_READ_BACK 1

static FATFS fat;
static struct fs_mount_t fixture_mount = {
    .type = FS_FATFS,
    .mnt_point = FIXTURE_ROOT,
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static void fixture_provision(void)
{
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&fixture_mount));
}

#endif /* CRUX_TESTS_CONFIGURATION_SETTINGS_FIXTURE_FATFS_H */
