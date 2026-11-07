/* The eMMC composition sequence on the eMMC simulator, with the cache flush on and Reliance Edge as
 * the file system. The tests run in name order: the File provider binds once per process, so "not
 * ready" cases come first. The medium is compared through emmc_sim_read_persistent, which leaves
 * out writes still held in the simulated cache, so every "unchanged" check also counts the write
 * requests that the simulator received. */
#include "emmc_sim.h"
#include "emmc_storage.h"
#include "fs_reliance_edge.h"
#include <crux/file.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

enum { SECTOR = 512, BATCH = 16, SECTORS = CONFIG_CRUX_EMMC_SIM_SECTORS };

static const bool init_failure_scenario = IS_ENABLED(CONFIG_EMMC_COMPOSITION_TEST_INIT_FAILURE);

/* Cases of the other scenario skip themselves. */
static void require_scenario(bool init_failure)
{
    if (init_failure_scenario != init_failure) {
        ztest_test_skip();
    }
}

/* FNV-1a over the persistent image: any write that reached the medium, a format or a directory
 * creation changes it. */
static uint32_t medium_digest(void)
{
    uint8_t data[SECTOR * BATCH];
    uint32_t hash = 2166136261u;

    for (uint32_t index = 0; index < SECTORS; index += BATCH) {
        zassert_ok(emmc_sim_read_persistent(index, data, BATCH));
        for (size_t i = 0; i < sizeof(data); i++) {
            hash = (hash ^ data[i]) * 16777619u;
        }
    }
    return hash;
}

static bool persistent_has_sector_starting_with(const void *pattern, size_t length)
{
    uint8_t data[SECTOR];

    for (uint32_t index = 0; index < SECTORS; index++) {
        zassert_ok(emmc_sim_read_persistent(index, data, 1));
        if (memcmp(data, pattern, length) == 0) {
            return true;
        }
    }
    return false;
}

static struct emmc_sim_stats stats(void)
{
    struct emmc_sim_stats out;

    emmc_sim_get_stats(&out);
    return out;
}

/* The digest to compare against, taken with the simulator's counters cleared. */
static uint32_t baseline(void)
{
    uint32_t digest = medium_digest();

    emmc_sim_reset_stats();
    return digest;
}

static void ensure_disk(void)
{
    zassert_ok(disk_access_init(EMMC_STORAGE_DISK));
}

/* Test-owned simulator media only: this is the host-side preparation of the card, with the
 * formatter that the composition itself never calls. The sync makes the image the one that a
 * power cut would leave, whatever the file system does at unmount. */
static void provision(bool files, bool settings)
{
    struct fs_mount_t mount = {
        .type = CRUX_FS_RELIANCE_EDGE,
        .mnt_point = EMMC_STORAGE_MOUNT,
        .storage_dev = (void *)EMMC_STORAGE_DISK,
    };

    ensure_disk();
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)EMMC_STORAGE_DISK, NULL, 0));
    zassert_ok(fs_mount(&mount));
    if (files) {
        zassert_ok(fs_mkdir(EMMC_STORAGE_FILE_DIR));
    }
    if (settings) {
        zassert_ok(fs_mkdir(EMMC_STORAGE_SETTINGS_DIR));
    }
    zassert_ok(fs_unmount(&mount));
    zassert_ok(disk_access_ioctl(EMMC_STORAGE_DISK, DISK_IOCTL_CTRL_SYNC, NULL));
}

static void expect_not_ready_and_untouched(uint32_t before)
{
    crux_file_t *file = NULL;

    zassert_equal(crux_file_open("probe", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_ERR_NOT_READY,
                  "File calls must stay not ready");
    zassert_equal(stats().writes, 0, "a failed start wrote to the medium");
    zassert_equal(medium_digest(), before, "a failed start changed the medium");
}

/* The digest must see any change to any sector, or the "unchanged medium" checks prove nothing. The
 * writes go through the disk and a sync, so that they leave the cache. */
ZTEST(file_emmc_composition, test_0_the_medium_digest_sees_a_single_changed_byte)
{
    uint8_t sector[SECTOR], changed[SECTOR];
    uint32_t before;

    require_scenario(false);
    ensure_disk();
    before = medium_digest();
    zassert_ok(disk_access_read(EMMC_STORAGE_DISK, sector, SECTORS - 1, 1));
    memcpy(changed, sector, sizeof(sector));
    changed[SECTOR - 1] ^= 1;
    zassert_ok(disk_access_write(EMMC_STORAGE_DISK, changed, SECTORS - 1, 1));
    zassert_ok(disk_access_ioctl(EMMC_STORAGE_DISK, DISK_IOCTL_CTRL_SYNC, NULL));
    zassert_not_equal(medium_digest(), before);
    zassert_ok(disk_access_write(EMMC_STORAGE_DISK, sector, SECTORS - 1, 1));
    zassert_ok(disk_access_ioctl(EMMC_STORAGE_DISK, DISK_IOCTL_CTRL_SYNC, NULL));
    zassert_equal(medium_digest(), before);
}

ZTEST(file_emmc_composition, test_0_stop_and_remount_before_start_are_refused)
{
    uint32_t before;

    require_scenario(false);
    before = baseline();
    zassert_equal(emmc_storage_stop(), -EINVAL);
    zassert_equal(emmc_storage_remount(), -EINVAL);
    zassert_equal(stats().writes, 0);
    zassert_equal(medium_digest(), before);
}

ZTEST(file_emmc_composition, test_a_blank_medium_fails_closed_and_is_never_formatted)
{
    uint32_t before;

    require_scenario(false);
    before = baseline();
    zassert_equal(emmc_storage_start(), EMMC_STORAGE_MOUNT_FAILED);
    expect_not_ready_and_untouched(before);
}

ZTEST(file_emmc_composition, test_b_missing_directories_are_not_created_by_the_firmware)
{
    uint32_t before;

    require_scenario(false);
    provision(false, false);
    before = baseline();
    zassert_equal(emmc_storage_start(), EMMC_STORAGE_LAYOUT_MISSING);
    expect_not_ready_and_untouched(before);
}

ZTEST(file_emmc_composition, test_c_one_missing_directory_is_still_not_ready)
{
    uint32_t before;

    require_scenario(false);
    provision(true, false);
    before = baseline();
    zassert_equal(emmc_storage_start(), EMMC_STORAGE_LAYOUT_MISSING);
    expect_not_ready_and_untouched(before);
}

ZTEST(file_emmc_composition, test_d_a_provisioned_medium_starts_after_earlier_failures)
{
    require_scenario(false);
    provision(true, true);

    zassert_equal(emmc_storage_start(), EMMC_STORAGE_READY, "no automatic retry was needed");
    crux_file_t *file = NULL;
    size_t count = 0;
    zassert_equal(crux_file_open("hello", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, "mm", 2, &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

ZTEST(file_emmc_composition, test_e_a_second_start_is_rejected_without_side_effects)
{
    uint32_t before;

    require_scenario(false);
    before = baseline();
    zassert_equal(emmc_storage_start(), EMMC_STORAGE_ALREADY_STARTED);
    zassert_equal(stats().writes, 0);
    zassert_equal(medium_digest(), before);
}

/* The data was synced before the stop, so it is also on the persistent image; the remount reads it
 * back through the file system. */
ZTEST(file_emmc_composition, test_f_the_provider_survives_an_application_unmount_and_remount)
{
    crux_file_t *file = NULL;
    size_t count = 0;
    char value[2] = {0};

    require_scenario(false);
    /* Reliance Edge refuses an unmount while a handle is open, so the application closes every
     * handle before stopping. */
    zassert_equal(emmc_storage_stop(), 0);
    zassert_equal(emmc_storage_stop(), -EINVAL, "a second stop must be refused");
    zassert_equal(crux_file_open("hello", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY,
                  "File calls must report not ready while the volume is unmounted");
    zassert_equal(emmc_storage_remount(), 0);
    zassert_equal(emmc_storage_remount(), -EALREADY, "a mounted volume must not be remounted");
    zassert_equal(crux_file_open("hello", CRUX_FILE_READ, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_read(file, value, sizeof(value), &count), CRUX_FILE_OK);
    zassert_equal(count, 2);
    zassert_mem_equal(value, "mm", 2);
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

/* A sync is the point where the data leaves the file system's buffers and the card's cache, so it
 * must send a flush command, and the bytes must then be in the image that survives a power cut. */
ZTEST(file_emmc_composition, test_g_file_sync_reaches_the_medium)
{
    static const uint8_t pattern[SECTOR / 2] = {
        'c', 'r', 'u', 'x', '-', 's', 'y', 'n', 'c', '-', 'p', 'a', 't', 't', 'e', 'r', 'n', '!',
    };
    crux_file_t *file = NULL;
    size_t count = 0;
    struct emmc_sim_stats before_sync;
    uint32_t before;

    require_scenario(false);
    zassert_equal(crux_file_open("synced", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_OK);
    zassert_equal(crux_file_write(file, pattern, sizeof(pattern), &count), CRUX_FILE_OK);
    zassert_equal(count, sizeof(pattern));
    before = baseline();
    before_sync = stats();
    zassert_equal(crux_file_sync(file), CRUX_FILE_OK);
    zassert_true(stats().flush_commands > before_sync.flush_commands,
                 "a sync reached the card without a flush command");
    zassert_not_equal(medium_digest(), before, "a sync left the persistent image unchanged");
    zassert_true(persistent_has_sector_starting_with(pattern, sizeof(pattern)),
                 "the synced bytes are not in the persistent image");
    zassert_equal(crux_file_close(&file), CRUX_FILE_OK);
}

/* The card does not answer its first CMD1, so the disk does not initialise. This runs in a process
 * where nothing initialised the disk before, which is why it has a scenario of its own. */
ZTEST(file_emmc_composition, test_h_card_init_failure_reports_no_media)
{
    crux_file_t *file = NULL;
    uint32_t before;

    require_scenario(true);
    before = baseline();
    emmc_sim_fail_command(1, 1, UINT32_MAX);
    zassert_equal(emmc_storage_start(), EMMC_STORAGE_NO_MEDIA);
    emmc_sim_clear_faults();
    zassert_equal(crux_file_open("probe", CRUX_FILE_CREATE_REPLACE, &file), CRUX_FILE_ERR_NOT_READY,
                  "File calls must stay not ready");
    zassert_equal(stats().writes, 0, "a failed init wrote to the medium");
    zassert_equal(medium_digest(), before);
    zassert_equal(emmc_storage_stop(), -EINVAL, "nothing was mounted");
}

ZTEST_SUITE(file_emmc_composition, NULL, NULL, NULL, NULL, NULL);
