/* The microSD composition sequence on a RAM disk named like the EVKB card. The tests run in
 * name order: the File provider binds once per process, so "not ready" cases come first. */
#include "sd_storage.h"
#include <crux/file.h>
#include <errno.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

enum { SECTOR = 512 };

/* FNV-1a over the whole medium: any write, format or directory creation changes it. */
static uint32_t medium_digest(void)
{
    uint8_t sector[SECTOR];
    uint32_t hash = 2166136261u;
    uint32_t sectors = 0;

    zassert_ok(disk_access_ioctl(SD_STORAGE_DISK, DISK_IOCTL_GET_SECTOR_COUNT, &sectors));
    for (uint32_t index = 0; index < sectors; index++) {
        zassert_ok(disk_access_read(SD_STORAGE_DISK, sector, index, 1));
        for (size_t i = 0; i < sizeof(sector); i++) {
            hash = (hash ^ sector[i]) * 16777619u;
        }
    }
    return hash;
}

static void provision(bool files, bool settings)
{
    static FATFS fat;
    struct fs_mount_t mount = {
        .type = FS_FATFS,
        .mnt_point = SD_STORAGE_MOUNT,
        .fs_data = &fat,
        .flags = FS_MOUNT_FLAG_NO_FORMAT,
    };

    /* Test-owned simulator media only: this is the host-side card preparation. */
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t)SD_STORAGE_DISK ":", NULL, 0));
    zassert_ok(fs_mount(&mount));
    if (files) {
        zassert_ok(fs_mkdir(SD_STORAGE_FILE_DIR));
    }
    if (settings) {
        zassert_ok(fs_mkdir(SD_STORAGE_SETTINGS_DIR));
    }
    zassert_ok(fs_unmount(&mount));
}

static void expect_not_ready_and_untouched(uint32_t before)
{
    crux_file_t *file = NULL;

    zassert_equal(crux_file_open("probe", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_ERR_NOT_READY,
                  "File calls must stay not ready");
    zassert_equal(medium_digest(), before, "a failed start changed the medium");
}

/* The digest must see any change to any sector, or the "unchanged medium" checks prove nothing. */
ZTEST(file_sd_composition, test_0_the_medium_digest_sees_a_single_changed_byte)
{
    uint8_t sector[SECTOR], changed[SECTOR];
    uint32_t sectors = 0, before = medium_digest();

    zassert_ok(disk_access_ioctl(SD_STORAGE_DISK, DISK_IOCTL_GET_SECTOR_COUNT, &sectors));
    zassert_ok(disk_access_read(SD_STORAGE_DISK, sector, sectors - 1, 1));
    memcpy(changed, sector, sizeof(sector));
    changed[SECTOR - 1] ^= 1;
    zassert_ok(disk_access_write(SD_STORAGE_DISK, changed, sectors - 1, 1));
    zassert_not_equal(medium_digest(), before);
    zassert_ok(disk_access_write(SD_STORAGE_DISK, sector, sectors - 1, 1));
    zassert_equal(medium_digest(), before);
}

ZTEST(file_sd_composition, test_0_stop_and_remount_before_start_are_refused)
{
    uint32_t before = medium_digest();

    zassert_equal(sd_storage_stop(), -EINVAL);
    zassert_equal(sd_storage_remount(), -EINVAL);
    zassert_equal(medium_digest(), before);
}

ZTEST(file_sd_composition, test_a_blank_medium_fails_closed_and_is_never_formatted)
{
    uint32_t before = medium_digest();

    zassert_equal(sd_storage_start(), SD_STORAGE_MOUNT_FAILED);
    expect_not_ready_and_untouched(before);
}

ZTEST(file_sd_composition, test_b_missing_directories_are_not_created_by_the_firmware)
{
    provision(false, false);
    uint32_t before = medium_digest();

    zassert_equal(sd_storage_start(), SD_STORAGE_LAYOUT_MISSING);
    expect_not_ready_and_untouched(before);
}

ZTEST(file_sd_composition, test_c_one_missing_directory_is_still_not_ready)
{
    provision(true, false);
    uint32_t before = medium_digest();

    zassert_equal(sd_storage_start(), SD_STORAGE_LAYOUT_MISSING);
    expect_not_ready_and_untouched(before);
}

ZTEST(file_sd_composition, test_d_a_provisioned_medium_starts_after_earlier_failures)
{
    provision(true, true);

    zassert_equal(sd_storage_start(), SD_STORAGE_READY, "no automatic retry was needed");
    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open("hello", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "sd", 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_sd_composition, test_e_a_second_start_is_rejected_without_side_effects)
{
    uint32_t before = medium_digest();

    zassert_equal(sd_storage_start(), SD_STORAGE_ALREADY_STARTED);
    zassert_equal(medium_digest(), before);
}

/* The remount reuses the in-process RAM disk, so this is not persistence evidence. */
ZTEST(file_sd_composition, test_f_the_provider_survives_an_application_unmount_and_remount)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[2] = {0};

    /* Zephyr's FatFs unmount does not refuse while a handle is open (observed on this
     * composition), so the application must close every handle before stopping. */
    zassert_equal(sd_storage_stop(), 0);
    zassert_equal(sd_storage_stop(), -EINVAL, "a second stop must be refused");
    zassert_equal(crux_file_open("hello", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY,
                  "File calls must report not ready while the card is unmounted");
    zassert_equal(sd_storage_remount(), 0);
    zassert_equal(sd_storage_remount(), -EALREADY, "a mounted volume must not be remounted");
    zassert_equal(crux_file_open("hello", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_mem_equal(value, "sd", 2);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST_SUITE(file_sd_composition, NULL, NULL, NULL, NULL, NULL);
