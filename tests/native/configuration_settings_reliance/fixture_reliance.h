/* SPDX-License-Identifier: Apache-2.0 */
/* Reliance Edge on a RAM disk named like the eMMC disk: the fixture that the Settings File suite
 * includes for the target provider. The medium is provisioned as an operator would, with the two
 * directories made before the settings are initialised, because the firmware makes none. */
#ifndef CRUX_TESTS_CONFIGURATION_SETTINGS_FIXTURE_RELIANCE_H
#define CRUX_TESTS_CONFIGURATION_SETTINGS_FIXTURE_RELIANCE_H

#include <fs_reliance_edge.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

#define FIXTURE_DISK "MMC"
#define FIXTURE_ROOT "/MMC:"

/* Not static: extra_cases.c unmounts and remounts the volume. */
struct fs_mount_t fixture_mount = {
    .type = CRUX_FS_RELIANCE_EDGE,
    .mnt_point = FIXTURE_ROOT,
    .storage_dev = (void *)FIXTURE_DISK,
};

/* The two directories of the composition's layout, on a mounted volume. */
static void fixture_make_layout(void)
{
    zassert_ok(fs_mkdir(FIXTURE_ROOT "/files"));
    zassert_ok(fs_mkdir(FIXTURE_ROOT "/settings"));
}

static void fixture_provision(void)
{
    zassert_ok(disk_access_init(FIXTURE_DISK));
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)FIXTURE_DISK, NULL, 0));
    zassert_ok(fs_mount(&fixture_mount));
    fixture_make_layout();
}

#endif /* CRUX_TESTS_CONFIGURATION_SETTINGS_FIXTURE_RELIANCE_H */
