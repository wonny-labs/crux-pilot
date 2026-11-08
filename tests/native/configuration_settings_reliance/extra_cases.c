/* SPDX-License-Identifier: Apache-2.0 */
/* Cases that only the target provider on Reliance Edge has: the compaction leaves no temporary
 * file, and emmc_configuration_start initialises the settings only on a layout that exists. In the
 * main scenario they run in the suite of main.c in tests/native/configuration_settings_file, so
 * they share its fixture, and each puts the volume back as it found it.
 *
 * The File root binds once per process, and a start that reaches the storage's ready state binds
 * it. So the start that succeeds and the start whose settings initialisation fails cannot share a
 * process: the second one reports that the storage is already started. The failing one is the
 * init_failure scenario, which runs only that case with a suite of its own, and every case of the
 * other scenario skips itself there, as the case of the other scenario does here. */
#include "configuration_settings.h"
#include "emmc_configuration.h"
#include "fs_reliance_edge.h"
#include <crux/configuration.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/ztest.h>

#ifdef CONFIG_SETTINGS_RELIANCE_TEST_INIT_FAILURE
#include "fixture_reliance.h"
#else
#define FIXTURE_DISK "MMC"
#define FIXTURE_ROOT "/MMC:"
extern struct fs_mount_t fixture_mount;
#endif
#define SETTINGS_FILE FIXTURE_ROOT "/settings/cfg"
#define SETTINGS_TEMP FIXTURE_ROOT "/settings/cfg.cmp"

enum { SECTOR = 512, SECTORS = 8192, BATCH = 16 };

static const bool init_failure_scenario = IS_ENABLED(CONFIG_SETTINGS_RELIANCE_TEST_INIT_FAILURE);

/* Cases of the other scenario skip themselves. */
static void require_scenario(bool init_failure)
{
    if (init_failure_scenario != init_failure) {
        ztest_test_skip();
    }
}

/* settings_subsys_init, counted, and made to fail once on request. */
int __real_settings_subsys_init(void);
static unsigned init_calls;
static bool init_fails;
int __wrap_settings_subsys_init(void)
{
    init_calls++;
    if (init_fails) {
        return -EIO;
    }
    return __real_settings_subsys_init();
}

static const crux_config_spec_t spec = {
    .key = "speed",
    .schema_version = 7,
    .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 11},
};

static void set_value(crux_config_param_t *p, uint32_t value)
{
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = value};

    zassert_ok(crux_config_update(p, &v));
}

static void expect_value(crux_config_param_t *p, uint32_t expected)
{
    crux_config_value_t v;

    zassert_ok(crux_config_read(p, &v));
    zassert_equal(v.data.u32, expected);
}

/* FNV-1a over every sector of the disk: a format, a directory or any write changes it. */
static uint32_t medium_digest(void)
{
    static uint8_t data[SECTOR * BATCH];
    uint32_t hash = 2166136261u;

    for (uint32_t sector = 0; sector < SECTORS; sector += BATCH) {
        zassert_ok(disk_access_read(FIXTURE_DISK, data, sector, BATCH));
        for (size_t i = 0; i < sizeof(data); i++) {
            hash = (hash ^ data[i]) * 16777619u;
        }
    }
    return hash;
}

static bool exists(const char *path)
{
    struct fs_dirent entry;

    return fs_stat(path, &entry) == 0;
}

/* Puts the suite's volume back: formatted, mounted, with the two directories of the layout. */
static void restore_volume(void)
{
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)FIXTURE_DISK, NULL, 0));
    zassert_ok(fs_mount(&fixture_mount));
    zassert_ok(fs_mkdir(FIXTURE_ROOT "/files"));
    zassert_ok(fs_mkdir(FIXTURE_ROOT "/settings"));
}

/* A compaction replaces the settings file through a temporary and a rename. With an atomic rename
 * the temporary is gone once the save returns, so a leftover would be a failed or interrupted
 * compaction. The file shrinks when the compaction ran, which is how the case knows it did. */
ZTEST(configuration_settings_reliance, test_compaction_leaves_no_cmp_file)
{
    require_scenario(false);
    crux_config_param_t p = {0};
    struct fs_dirent entry;
    size_t previous = 0;
    unsigned compactions = 0;

    /* Cases of the shared suite that inject a fault into a compaction leave their temporary behind,
     * and before() of the suite resets only the settings file. */
    int rc = fs_unlink(SETTINGS_TEMP);

    zassert_true(rc == 0 || rc == -ENOENT, "removing an old temporary gave %d", rc);
    zassert_ok(crux_config_init(&p, &spec));
    for (uint32_t i = 1; i <= 400; i++) {
        set_value(&p, 1000 + i);
        zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK, "save %u", i);
        zassert_false(exists(SETTINGS_TEMP), "a temporary file is left after save %u", i);
        zassert_ok(fs_stat(SETTINGS_FILE, &entry));
        if (entry.size < previous) {
            compactions++;
        }
        previous = entry.size;
    }
    zassert_true(compactions > 0, "400 saves never compacted the file");
    set_value(&p, 1);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, 1400);
}

/* A medium that holds a Reliance Edge volume and no directories: the Settings backend would make
 * /settings itself on init, so the composition must stop before it. Nothing is written. */
ZTEST(configuration_settings_reliance,
      test_start_on_a_medium_without_settings_dir_does_not_init_settings)
{
    require_scenario(false);
    uint32_t before;

    zassert_ok(fs_unmount(&fixture_mount));
    zassert_ok(fs_mkfs(CRUX_FS_RELIANCE_EDGE, (uintptr_t)FIXTURE_DISK, NULL, 0));
    before = medium_digest();
    init_calls = 0;

    zassert_equal(emmc_configuration_start(), EMMC_STORAGE_LAYOUT_MISSING);
    zassert_equal(init_calls, 0U, "the settings were initialised without a layout");
    zassert_equal(medium_digest(), before, "the medium changed");

    zassert_ok(fs_mount(&fixture_mount));
    zassert_false(exists(FIXTURE_ROOT "/settings"), "a directory was made");
    zassert_ok(fs_unmount(&fixture_mount));
    restore_volume();
}

/* The same sequence on a provisioned medium: the storage is ready and the settings are initialised
 * once, and Configuration then saves and loads through the file system that start mounted. */
ZTEST(configuration_settings_reliance, test_start_on_a_provisioned_medium_inits_settings_once)
{
    require_scenario(false);
    crux_config_param_t p = {0};

    zassert_ok(fs_unmount(&fixture_mount));
    init_calls = 0;
    zassert_equal(emmc_configuration_start(), EMMC_STORAGE_READY);
    zassert_equal(init_calls, 1U);

    zassert_ok(crux_config_init(&p, &spec));
    set_value(&p, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 1);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, 42);

    /* A second start is refused by the storage and initialises nothing. */
    zassert_equal(emmc_configuration_start(), EMMC_STORAGE_ALREADY_STARTED);
    zassert_equal(init_calls, 1U);

    zassert_ok(emmc_storage_stop());
    zassert_ok(fs_mount(&fixture_mount));
}

/* A settings initialisation that fails after a ready storage is reported as its own status, the
 * storage stays started, and the settings can be initialised again directly. */
ZTEST(configuration_settings_reliance, test_a_failed_settings_init_is_reported)
{
    crux_config_param_t p = {0};
    enum emmc_storage_status status;

    require_scenario(true);
    zassert_ok(fs_unmount(&fixture_mount));
    init_calls = 0;
    init_fails = true;
    status = emmc_configuration_start();
    init_fails = false;
    zassert_equal(status, EMMC_STORAGE_CONFIGURATION_FAILED);
    zassert_equal(init_calls, 1U);

    /* The storage is started, so another start retries nothing. */
    zassert_equal(emmc_configuration_start(), EMMC_STORAGE_ALREADY_STARTED);
    zassert_equal(init_calls, 1U);

    zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_OK);
    zassert_equal(init_calls, 2U);
    zassert_ok(crux_config_init(&p, &spec));
    set_value(&p, 9);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 1);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, 9);
    zassert_ok(emmc_storage_stop());
}

#ifdef CONFIG_SETTINGS_RELIANCE_TEST_INIT_FAILURE
static void *provision(void)
{
    fixture_provision();
    return NULL;
}

ZTEST_SUITE(configuration_settings_reliance, NULL, provision, NULL, NULL, NULL);
#endif
