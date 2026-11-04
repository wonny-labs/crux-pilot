/* The recovery step for the one state a power cut can leave in a Settings File compaction: no
 * settings file and a complete temporary (samples/storage_sd/sd_settings_recover). The states are
 * created here; the real cut is only exercised on the board. Everything but the recovery step runs
 * on the real, unpatched Settings File backend over a FatFs RAM disk named like the card. */
#include "sd_settings_recover.h"
#include <errno.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#define DIR_PATH "/SD:/settings"
#define CFG DIR_PATH "/cfg"
#define CMP DIR_PATH "/cfg.cmp"
#define LONG_CFG DIR_PATH "/a-long-settings-file-name"
#define FIT_CFG DIR_PATH "/cfgabcdefghij"   /* 27 bytes: "<path>.cmp" is the backend's 31 */
#define OVER_CFG DIR_PATH "/cfgabcdefghijk" /* 28 bytes: the backend shortens the temporary */

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/SD:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};

static int stat_fault; /* error returned by fs_stat for the path in fault_path, 0 for none */
static const char *fault_path;
static int rename_fault;
static int rename_calls;

int __real_fs_stat(const char *path, struct fs_dirent *entry);
int __real_fs_rename(const char *from, const char *to);

int __wrap_fs_stat(const char *path, struct fs_dirent *entry)
{
    if (stat_fault != 0 && fault_path != NULL && strcmp(path, fault_path) == 0) {
        return stat_fault;
    }
    return __real_fs_stat(path, entry);
}

int __wrap_fs_rename(const char *from, const char *to)
{
    rename_calls++;
    return rename_fault != 0 ? rename_fault : __real_fs_rename(from, to);
}

static uint32_t value;
static int loads;

static int app_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(name, "v") != 0 || len != sizeof(value)) {
        return -ENOENT;
    }
    loads++;
    return read_cb(cb_arg, &value, sizeof(value)) == (ssize_t)sizeof(value) ? 0 : -EIO;
}
SETTINGS_STATIC_HANDLER_DEFINE(app, "app", NULL, app_set, NULL, NULL);

static void *setup(void)
{
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "SD:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_ok(fs_mkdir(DIR_PATH));
    zassert_ok(settings_subsys_init());
    return NULL;
}

static void remove_if_present(const char *path)
{
    struct fs_dirent entry;

    if (__real_fs_stat(path, &entry) == 0) {
        zassert_ok(fs_unlink(path));
    }
}

static void before(void *unused)
{
    ARG_UNUSED(unused);
    stat_fault = rename_fault = rename_calls = 0;
    fault_path = NULL;
    value = loads = 0;
    remove_if_present(CFG);
    remove_if_present(CMP);
    remove_if_present(LONG_CFG);
    remove_if_present(LONG_CFG ".cmp");
    remove_if_present(FIT_CFG);
    remove_if_present(FIT_CFG ".cmp");
    remove_if_present(OVER_CFG);
    remove_if_present(OVER_CFG ".cmp");
}

static size_t read_all(const char *path, uint8_t *buf, size_t size)
{
    struct fs_file_t file;
    ssize_t n;

    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, path, FS_O_READ));
    n = fs_read(&file, buf, size);
    zassert_true(n >= 0);
    zassert_ok(fs_close(&file));
    return (size_t)n;
}

static void write_all(const char *path, const uint8_t *data, size_t size)
{
    struct fs_file_t file;

    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, path, FS_O_CREATE | FS_O_WRITE));
    if (size > 0) {
        zassert_equal(fs_write(&file, data, size), (ssize_t)size);
    }
    zassert_ok(fs_close(&file));
}

static bool exists(const char *path)
{
    struct fs_dirent entry;
    return __real_fs_stat(path, &entry) == 0;
}

/* The settings file the real backend wrote for "app/v" = v. */
static size_t save_with_backend(uint32_t v, uint8_t *image, size_t size)
{
    zassert_ok(settings_save_one("app/v", &v, sizeof(v)));
    return read_all(CFG, image, size);
}

/* The state a cut leaves between the removal and the rename: the backend's own bytes, but only
 * under the temporary name. */
static size_t make_the_window(uint32_t v, uint8_t *image, size_t size)
{
    size_t n = save_with_backend(v, image, size);

    zassert_ok(fs_unlink(CFG));
    write_all(CMP, image, n);
    return n;
}

ZTEST(file_sd_settings_recover, test_nothing_to_recover_when_neither_file_exists)
{
    zassert_equal(sd_settings_recover(CFG), 0);
    zassert_false(exists(CFG));
    zassert_false(exists(CMP));
    zassert_equal(rename_calls, 0);
}

ZTEST(file_sd_settings_recover, test_the_window_loses_the_value_without_the_recovery_step)
{
    uint8_t image[128];

    (void)make_the_window(7, image, sizeof(image));
    zassert_ok(settings_load_subtree("app"));
    zassert_equal(loads, 0, "the backend found a value in a state without a settings file");
}

ZTEST(file_sd_settings_recover, test_the_window_keeps_the_newest_value_with_the_recovery_step)
{
    uint8_t image[128], after[128];
    size_t n = make_the_window(7, image, sizeof(image));

    zassert_equal(sd_settings_recover(CFG), SD_SETTINGS_RECOVERED);
    zassert_false(exists(CMP));
    zassert_equal(read_all(CFG, after, sizeof(after)), n);
    zassert_mem_equal(after, image, n);
    zassert_ok(settings_load_subtree("app"));
    zassert_equal(loads, 1);
    zassert_equal(value, 7);
}

ZTEST(file_sd_settings_recover, test_a_second_call_after_recovery_does_nothing)
{
    uint8_t image[128];

    (void)make_the_window(9, image, sizeof(image));
    zassert_equal(sd_settings_recover(CFG), SD_SETTINGS_RECOVERED);
    zassert_equal(sd_settings_recover(CFG), 0);
    zassert_equal(rename_calls, 1);
}

ZTEST(file_sd_settings_recover, test_an_existing_settings_file_is_never_replaced_by_a_temporary)
{
    uint8_t image[128], before_cfg[128], after_cfg[128], stale[4] = {1, 2, 3, 4};
    size_t n = save_with_backend(5, image, sizeof(image));

    memcpy(before_cfg, image, n);
    write_all(CMP, stale, sizeof(stale));
    zassert_equal(sd_settings_recover(CFG), 0);
    zassert_equal(read_all(CFG, after_cfg, sizeof(after_cfg)), n);
    zassert_mem_equal(after_cfg, before_cfg, n);
    zassert_true(exists(CMP));
    zassert_equal(rename_calls, 0);
}

ZTEST(file_sd_settings_recover, test_an_empty_temporary_is_not_a_finished_compaction)
{
    write_all(CMP, NULL, 0);
    zassert_equal(sd_settings_recover(CFG), -ENODATA);
    zassert_false(exists(CFG));
    zassert_true(exists(CMP));
    zassert_equal(rename_calls, 0);
}

ZTEST(file_sd_settings_recover, test_a_directory_is_never_renamed_over_or_from)
{
    uint8_t data[4] = {1, 2, 3, 4};

    zassert_ok(fs_mkdir(CFG));
    write_all(CMP, data, sizeof(data));
    zassert_equal(sd_settings_recover(CFG), -EISDIR);
    zassert_equal(rename_calls, 0);
    zassert_ok(fs_unlink(CFG));
    zassert_ok(fs_unlink(CMP));
    zassert_ok(fs_mkdir(CMP));
    zassert_equal(sd_settings_recover(CFG), -EISDIR);
    zassert_equal(rename_calls, 0);
    zassert_ok(fs_unlink(CMP));
}

ZTEST(file_sd_settings_recover, test_a_stat_error_on_the_settings_file_is_not_a_missing_file)
{
    uint8_t image[128];

    (void)make_the_window(3, image, sizeof(image));
    stat_fault = -EIO;
    fault_path = CFG;
    zassert_equal(sd_settings_recover(CFG), -EIO);
    zassert_equal(rename_calls, 0);
    zassert_true(exists(CMP));
}

ZTEST(file_sd_settings_recover, test_a_stat_error_on_the_temporary_is_not_a_missing_temporary)
{
    uint8_t image[128];

    (void)make_the_window(3, image, sizeof(image));
    stat_fault = -EIO;
    fault_path = CMP;
    zassert_equal(sd_settings_recover(CFG), -EIO);
    zassert_equal(rename_calls, 0);
    stat_fault = 0;
    zassert_true(exists(CMP));
    zassert_false(exists(CFG));
}

ZTEST(file_sd_settings_recover, test_a_failed_rename_is_reported_and_leaves_the_temporary)
{
    uint8_t image[128];

    (void)make_the_window(4, image, sizeof(image));
    rename_fault = -EIO;
    zassert_equal(sd_settings_recover(CFG), -EIO);
    zassert_true(exists(CMP));
    zassert_false(exists(CFG));
    rename_fault = 0;
    zassert_equal(sd_settings_recover(CFG), SD_SETTINGS_RECOVERED); /* a retry completes it */
}

ZTEST(file_sd_settings_recover, test_a_path_the_backend_would_truncate_is_refused)
{
    uint8_t data[4] = {1, 2, 3, 4};

    write_all(LONG_CFG ".cmp", data, sizeof(data));
    zassert_equal(sd_settings_recover(LONG_CFG), -ENAMETOOLONG);
    zassert_equal(rename_calls, 0);
    zassert_false(exists(LONG_CFG));
}

ZTEST(file_sd_settings_recover, test_the_longest_path_with_a_whole_temporary_name_is_recovered)
{
    uint8_t data[4] = {1, 2, 3, 4}, after[4];

    write_all(FIT_CFG ".cmp", data, sizeof(data));
    zassert_equal(sd_settings_recover(FIT_CFG), SD_SETTINGS_RECOVERED);
    zassert_equal(read_all(FIT_CFG, after, sizeof(after)), sizeof(after));
    zassert_mem_equal(after, data, sizeof(data));
    zassert_false(exists(FIT_CFG ".cmp"));
}

ZTEST(file_sd_settings_recover, test_the_shortest_path_whose_temporary_name_is_shortened_is_refused)
{
    uint8_t data[4] = {1, 2, 3, 4};

    write_all(OVER_CFG ".cmp", data, sizeof(data));
    zassert_equal(sd_settings_recover(OVER_CFG), -ENAMETOOLONG);
    zassert_equal(rename_calls, 0);
    zassert_false(exists(OVER_CFG));
}

ZTEST_SUITE(file_sd_settings_recover, NULL, setup, before, NULL, NULL);
