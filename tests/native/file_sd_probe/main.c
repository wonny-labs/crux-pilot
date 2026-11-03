/* The workload and the removal state machine of the physical-gate probes (samples/storage_sd_probe)
 * on a RAM disk named like the EVKB card. A removed card is stood in for by an application unmount;
 * real removal is exercised only on the board. Tests run in name order: the File provider binds
 * once. */
#include "sd_probe.h"
#include "sd_storage.h"
#include <crux/file.h>
#include <errno.h>
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

ZTEST(file_sd_probe, test_a_the_workload_completes_on_real_fatfs_and_leaves_no_files_behind)
{
    struct fs_dirent entry;

    provision();
    zassert_equal(sd_storage_start(), SD_STORAGE_READY);
    zassert_equal(sd_probe_workload(), 0);
    zassert_equal(sd_probe_workload(), 0, "the workload must be repeatable");
    zassert_not_equal(fs_stat(SD_STORAGE_SETTINGS_DIR "/probe.a", &entry), 0);
    zassert_not_equal(fs_stat(SD_STORAGE_SETTINGS_DIR "/probe.b", &entry), 0);
}

ZTEST(file_sd_probe, test_b_steps_succeed_while_the_card_is_present)
{
    struct sd_probe probe;

    sd_probe_init(&probe);
    for (int i = 0; i < 3; i++) {
        sd_probe_step(&probe);
    }
    zassert_equal(probe.state, SD_PROBE_OK);
    zassert_equal(probe.ops, 3);
    zassert_equal(probe.failures, 0);
}

ZTEST(file_sd_probe, test_c_a_removal_is_detected_then_a_remount_recovers_without_a_reboot)
{
    struct sd_probe probe;

    sd_probe_init(&probe);
    sd_probe_step(&probe);
    zassert_equal(probe.state, SD_PROBE_OK);

    zassert_equal(sd_storage_stop(), 0); /* stands in for the card disappearing */
    sd_probe_step(&probe);
    zassert_equal(probe.state, SD_PROBE_LOST, "a failed operation must be reported");
    zassert_equal(probe.failures, 1);
    zassert_not_equal(probe.codes[0], 0, "the failing result code must be kept");

    sd_probe_step(&probe); /* while lost each step tries to bring the volume back */
    zassert_equal(probe.remount, 0);
    zassert_equal(probe.state, SD_PROBE_RECOVERED);

    sd_probe_step(&probe);
    zassert_equal(probe.state, SD_PROBE_OK);
    zassert_equal(probe.recoveries, 1);
}

ZTEST(file_sd_probe, test_d_the_first_failure_unmounts_the_volume_itself)
{
    struct sd_probe probe;
    crux_file_t *held = NULL;
    crux_file_t *other = NULL;

    zassert_equal(sd_storage_remount(), -EALREADY, "the volume must be mounted here");
    /* A writer holds the probe file, so the step's open is refused while the volume stays mounted.
     */
    zassert_equal(crux_file_open("probe", CRUX_FILE_CREATE_REPLACE, &held), CRUX_FILE_OK);
    sd_probe_init(&probe);
    sd_probe_step(&probe);
    zassert_equal(probe.state, SD_PROBE_LOST);
    zassert_equal(probe.codes[0], CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(crux_file_open("other", CRUX_FILE_CREATE_REPLACE, &other),
                  CRUX_FILE_ERR_NOT_READY, "the failing step must have unmounted the volume");
    (void)crux_file_close(&held); /* a stale handle: the result does not matter */
    zassert_equal(sd_storage_remount(), 0);
}

ZTEST_SUITE(file_sd_probe, NULL, NULL, NULL, NULL, NULL);
