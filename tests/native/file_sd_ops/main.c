/* The seek and remove check of samples/storage_sd on a RAM disk named like the EVKB card. It runs
 * in name order: the File provider binds once per process. Nothing here is hardware evidence. */
#include "sd_file_ops.h"
#include "sd_storage.h"
#include <crux/file.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/ztest.h>

static void provision(void)
{
    static FATFS fat;
    struct fs_mount_t mount = {
        .type = FS_FATFS,
        .mnt_point = SD_STORAGE_MOUNT,
        .fs_data = &fat,
        .flags = FS_MOUNT_FLAG_NO_FORMAT,
    };

    /* Test-owned simulator media only: the host-side card preparation. */
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t)SD_STORAGE_DISK ":", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_ok(fs_mkdir(SD_STORAGE_FILE_DIR));
    zassert_ok(fs_mkdir(SD_STORAGE_SETTINGS_DIR));
    zassert_ok(fs_unmount(&mount));
}

ZTEST(file_sd_ops, test_a_seek_and_remove_pass_and_leave_no_file)
{
    crux_file_t *file = NULL;

    provision();
    zassert_equal(sd_storage_start(), SD_STORAGE_READY);
    zassert_equal(sd_file_ops_run(), SD_FILE_OPS_OK);
    zassert_equal(crux_file_open(SD_FILE_OPS_NAME, CRUX_FILE_READ, &file),
                  CRUX_FILE_ERR_NOT_FOUND, "the scratch file must be gone after the check");
}

ZTEST(file_sd_ops, test_b_a_second_run_passes_the_same_way)
{
    zassert_equal(sd_file_ops_run(), SD_FILE_OPS_OK);
}

ZTEST(file_sd_ops, test_c_a_leftover_scratch_file_is_replaced_not_trusted)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    static const char junk[] = "left behind by an earlier interrupted run";

    zassert_equal(crux_file_open(SD_FILE_OPS_NAME, CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, junk, sizeof(junk), &count), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
    zassert_equal(sd_file_ops_run(), SD_FILE_OPS_OK);
}

ZTEST(file_sd_ops, test_d_an_unmounted_volume_is_reported_not_passed)
{
    zassert_equal(sd_storage_stop(), 0);
    zassert_equal(sd_file_ops_run(), SD_FILE_OPS_CREATE_FAILED);
}

ZTEST_SUITE(file_sd_ops, NULL, NULL, NULL, NULL, NULL);
