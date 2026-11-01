/* SPDX-License-Identifier: Apache-2.0 */
#include "configuration_settings.h"
#include <crux/configuration.h>
#include <errno.h>
#include <ff.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

static FATFS fat;
static struct fs_mount_t mount = {
    .type = FS_FATFS,
    .mnt_point = "/RAM:",
    .fs_data = &fat,
    .flags = FS_MOUNT_FLAG_NO_FORMAT,
};
static bool fail_read;
/* Compaction faults: the compaction temp file (*.cmp) marks the start of a compaction. */
static bool compaction_started, fail_close_in_compaction, fail_read_in_compaction;
static unsigned write_count, inject_at;
static unsigned compaction_read_fault_at; /* fail the N-th read after the next compaction starts */
static bool count_compaction_reads;
extern bool crux_probe_read_back; /* provider copy: verify every saved record by reading it back */
static unsigned read_calls, fail_read_at; /* fail the N-th fs_read since the counter was reset */
enum write_fault { WRITE_NONE, WRITE_ZERO, WRITE_SHORT, WRITE_ERROR };
static enum write_fault write_fault;

int __real_fs_open(struct fs_file_t *, const char *, fs_mode_t);
int __real_fs_close(struct fs_file_t *);
ssize_t __real_fs_read(struct fs_file_t *, void *, size_t);
ssize_t __real_fs_write(struct fs_file_t *, const void *, size_t);
int __wrap_fs_open(struct fs_file_t *file, const char *path, fs_mode_t flags)
{
    if (strstr(path, ".cmp") != NULL) {
        compaction_started = true;
        if (compaction_read_fault_at != 0) {
            read_calls = 0;
            fail_read_at = compaction_read_fault_at;
            compaction_read_fault_at = 0;
        } else if (count_compaction_reads) {
            read_calls = 0;
        }
    }
    return __real_fs_open(file, path, flags);
}
int __wrap_fs_close(struct fs_file_t *file)
{
    int rc = __real_fs_close(file); /* always release the handle, then report the fault */
    if (compaction_started && fail_close_in_compaction) {
        fail_close_in_compaction = false;
        return -EIO;
    }
    return rc;
}
ssize_t __wrap_fs_read(struct fs_file_t *file, void *buffer, size_t size)
{
    ++read_calls;
    if (fail_read_at != 0 && read_calls == fail_read_at) {
        fail_read_at = 0;
        return -EIO;
    }
    if (compaction_started && fail_read_in_compaction) {
        fail_read_in_compaction = false;
        return -EIO;
    }
    if (fail_read) {
        fail_read = false;
        return -EIO;
    }
    return __real_fs_read(file, buffer, size);
}
ssize_t __wrap_fs_write(struct fs_file_t *file, const void *buffer, size_t size)
{
    ++write_count;
    if (write_count == inject_at) {
        if (write_fault == WRITE_ZERO) {
            return 0;
        }
        if (write_fault == WRITE_SHORT) {
            return size > 1 ? __real_fs_write(file, buffer, size - 1) : 0;
        }
        if (write_fault == WRITE_ERROR) {
            return -EIO;
        }
    }
    return __real_fs_write(file, buffer, size);
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
static bool short_record;
/* A single test source supplies a callback short count, which the Settings API itself cannot
 * produce; every other case uses only the real Settings File store. */
static ssize_t short_read_cb(void *arg, void *data, size_t length)
{
    ARG_UNUSED(arg);
    memset(data, 0, length);
    return (ssize_t)length - 1;
}
static int short_load(struct settings_store *store, const struct settings_load_arg *arg)
{
    ARG_UNUSED(store);
    return short_record ? settings_call_set_handler("cfg/short", 8, short_read_cb, NULL, arg) : 0;
}
static const struct settings_store_itf short_itf = {.csi_load = short_load};
static struct settings_store short_store = {.cs_itf = &short_itf};

static void *setup(void)
{
    zassert_ok(fs_mkfs(FS_FATFS, (uintptr_t) "RAM:", NULL, 0));
    zassert_ok(fs_mount(&mount));
    zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_OK);
    settings_src_register(&short_store);
    return NULL;
}
static void reset_file(void);

static void before(void *unused)
{
    ARG_UNUSED(unused);
    fail_read = false;
    read_calls = fail_read_at = 0;
    compaction_read_fault_at = 0;
    count_compaction_reads = false;
    crux_probe_read_back = false;
    short_record = false;
    compaction_started = fail_close_in_compaction = fail_read_in_compaction = false;
    write_count = inject_at = 0;
    write_fault = WRITE_NONE;
    reset_file(); /* an explicitly provisioned but empty settings file */
}

ZTEST(configuration_settings_file, test_missing_and_valid_roundtrip)
{
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &spec));
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_MISSING);
    set_value(&p, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 99);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, 42);
}

ZTEST(configuration_settings_file, test_scan_read_error_is_not_missing)
{
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &spec));
    set_value(&p, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 99);
    fail_read = true;
    crux_config_result_t rc = crux_config_load(&p);
    zassert_false(fail_read, "fault must reach the real backend");
    zassert_equal(rc, CRUX_CONFIG_ERR_IO, "an unreadable store is not a missing key (got %d)", rc);
    expect_value(&p, 99);
}

ZTEST(configuration_settings_file, test_save_scan_error_is_not_success)
{
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &spec));
    set_value(&p, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 99);
    fail_read = true;
    crux_config_result_t rc = crux_config_save(&p);
    zassert_false(fail_read, "fault must reach the duplicate scan");
    zassert_equal(rc, CRUX_CONFIG_ERR_IO, "an uncertain duplicate scan cannot succeed (got %d)",
                  rc);
    expect_value(&p, 99);
}

static void check_write_fault(enum write_fault fault, unsigned index)
{
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &spec));
    set_value(&p, 42);
    write_count = 0;
    write_fault = fault;
    inject_at = index;
    crux_config_result_t rc = crux_config_save(&p);
    write_fault = WRITE_NONE;
    zassert_true(write_count >= index, "fault must reach the real backend");
    zassert_equal(rc, CRUX_CONFIG_ERR_IO,
                  "partial or failed record writes cannot succeed (got %d, fs_write calls %u)", rc,
                  write_count);
    expect_value(&p, 42);
}
ZTEST(configuration_settings_file, test_zero_write_is_failure)
{
    check_write_fault(WRITE_ZERO, 1);
}
ZTEST(configuration_settings_file, test_short_write_is_failure)
{
    check_write_fault(WRITE_SHORT, 1);
}
ZTEST(configuration_settings_file, test_name_write_error_is_failure)
{
    check_write_fault(WRITE_ERROR, 2);
}
static const crux_config_spec_t other_spec = {
    .key = "limit",
    .schema_version = 7,
    .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 3},
};

/* Save distinct values until the next save starts a compaction; returns that save's result. */
static crux_config_result_t save_until_compaction(crux_config_param_t *p)
{
    for (uint32_t i = 1; i <= 400; i++) {
        set_value(p, 1000 + i);
        compaction_started = false;
        crux_config_result_t rc = crux_config_save(p);
        if (compaction_started) {
            return rc;
        }
        zassert_equal(rc, CRUX_CONFIG_OK, "plain save %u failed (got %d)", i, rc);
    }
    zassert_unreachable("no compaction within 400 saves");
    return CRUX_CONFIG_ERR_IO;
}

ZTEST(configuration_settings_file, test_compaction_close_error_is_not_success)
{
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &spec));
    fail_close_in_compaction = true;
    crux_config_result_t rc = save_until_compaction(&p);
    zassert_false(fail_close_in_compaction, "fault must reach the compaction close");
    zassert_equal(rc, CRUX_CONFIG_ERR_IO, "a failed compaction close cannot be success (got %d)",
                  rc);
}

ZTEST(configuration_settings_file, test_compaction_read_error_keeps_other_records)
{
    crux_config_param_t p = {0}, other = {0};
    zassert_ok(crux_config_init(&p, &spec));
    zassert_ok(crux_config_init(&other, &other_spec));
    set_value(&other, 7);
    zassert_equal(crux_config_save(&other), CRUX_CONFIG_OK);
    fail_read_in_compaction = true;
    crux_config_result_t rc = save_until_compaction(&p);
    zassert_false(fail_read_in_compaction, "fault must reach the compaction scan");
    set_value(&other, 99);
    crux_config_result_t loaded = crux_config_load(&other);
    /* The aborted compaction keeps the original file, so the other record stays readable. */
    zassert_equal(loaded, CRUX_CONFIG_OK, "another record was lost (save %d, load %d)", rc, loaded);
    expect_value(&other, 7);
    zassert_not_equal(rc, CRUX_CONFIG_OK, "the failed compaction was acknowledged");
}

ZTEST(configuration_settings_file, test_truncated_tail_keeps_earlier_records)
{
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &spec));
    set_value(&p, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    /* An interrupted append leaves a partial length field after the last good record. */
    struct fs_file_t file;
    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, "/RAM:/settings/cfg", FS_O_RDWR));
    zassert_ok(fs_seek(&file, 0, FS_SEEK_END));
    zassert_equal(fs_write(&file, "\x01", 1), 1);
    zassert_ok(fs_close(&file));
    set_value(&p, 99);
    crux_config_result_t rc = crux_config_load(&p);
    zassert_equal(rc, CRUX_CONFIG_OK, "a torn tail must not hide earlier records (got %d)", rc);
    expect_value(&p, 42);
}

static void remount(void)
{
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
}

static void fresh(crux_config_param_t *p, const crux_config_spec_t *s, uint32_t value)
{
    zassert_ok(crux_config_init(p, s));
    set_value(p, value);
}

static void fill_volume(void)
{
    static const uint8_t chunk[512];
    struct fs_file_t file;
    ssize_t written;

    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, "/RAM:/filler", FS_O_CREATE | FS_O_WRITE));
    do {
        written = fs_write(&file, chunk, sizeof(chunk));
    } while (written == (ssize_t)sizeof(chunk));
    (void)fs_close(&file);
}

ZTEST(configuration_settings_file, test_callback_short_count_preserves_active)
{
    crux_config_spec_t short_spec = spec;
    crux_config_param_t p = {0};

    short_spec.key = "short";
    fresh(&p, &short_spec, 99);
    short_record = true;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_value(&p, 99);
}

ZTEST(configuration_settings_file, test_staging_limit_boundary)
{
    uint8_t wire[257] = {1, 2, 7, 0, 42};
    crux_config_param_t p = {0};

    fresh(&p, &spec, 42);
    zassert_ok(settings_save_one("cfg/speed", wire, 256));
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_REJECTED);
    expect_value(&p, 11);
    set_value(&p, 99);
    zassert_ok(settings_save_one("cfg/speed", wire, 257));
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_value(&p, 99);
}

/* A save can legitimately succeed on a full volume while the settings file still has room in its
 * last cluster, so keep saving until one fails. Every save that was acknowledged must then be
 * retrievable, and saving must work again once space is freed. */
ZTEST(configuration_settings_file, test_full_volume_never_acknowledges_a_lost_save)
{
    crux_config_param_t p = {0};
    uint32_t last_acknowledged = 1000;
    bool failed = false;

    fresh(&p, &spec, last_acknowledged);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    fill_volume();
    for (uint32_t value = 1001; value < 3000 && !failed; value++) {
        set_value(&p, value);
        crux_config_result_t rc = crux_config_save(&p);
        if (rc == CRUX_CONFIG_OK) {
            last_acknowledged = value;
            continue;
        }
        zassert_true(rc == CRUX_CONFIG_ERR_NO_SPACE || rc == CRUX_CONFIG_ERR_IO, "got %d", rc);
        failed = true;
    }
    zassert_true(failed, "2000 saves to a full volume never failed: saves were lost silently");
    zassert_ok(fs_unlink("/RAM:/filler"));
    set_value(&p, 1);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, last_acknowledged);
    set_value(&p, 7);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 1);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, 7);
}

ZTEST(configuration_settings_file, test_roundtrip_survives_remount_for_u32_and_bool)
{
    static const crux_config_spec_t bool_spec = {
        .key = "enabled",
        .schema_version = 1,
        .default_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = false},
    };
    crux_config_param_t p = {0}, b = {0};
    crux_config_value_t flag = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};

    fresh(&p, &spec, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    zassert_ok(crux_config_init(&b, &bool_spec));
    zassert_ok(crux_config_update(&b, &flag));
    zassert_equal(crux_config_save(&b), CRUX_CONFIG_OK);
    remount();
    set_value(&p, 1);
    zassert_ok(crux_config_update(&b, &bool_spec.default_value));
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    zassert_equal(crux_config_load(&b), CRUX_CONFIG_OK);
    expect_value(&p, 42);
    zassert_ok(crux_config_read(&b, &flag));
    zassert_true(flag.data.boolean);
}

ZTEST(configuration_settings_file, test_each_load_read_fault_preserves_active)
{
    static const uint8_t unrelated[5] = {1, 1, 1, 0, 1};
    crux_config_param_t p = {0};
    unsigned bound, failing = 0, first_failing = 0;

    fresh(&p, &spec, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    zassert_ok(settings_save_one("cfg/other", unrelated, sizeof(unrelated)));
    remount();
    read_calls = 0;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    bound = read_calls;
    zassert_true(bound > 0 && bound < 128, "a load reads %u times", bound);
    for (unsigned i = 1; i <= bound; ++i) {
        remount();
        set_value(&p, 99);
        read_calls = 0;
        fail_read_at = i;
        crux_config_result_t rc = crux_config_load(&p);
        fail_read_at = 0;
        crux_config_value_t now;
        zassert_ok(crux_config_read(&p, &now));
        /* Safe outcomes: the fault is reported and the active value is kept, or the fault did not
         * affect the record and the stored value is delivered. A fault that reads as "missing" or
         * changes the active value in any other way is a hidden failure. */
        bool reported = rc == CRUX_CONFIG_ERR_IO && now.data.u32 == 99;
        bool harmless = rc == CRUX_CONFIG_OK && now.data.u32 == 42;
        if (!reported && !harmless) {
            failing++;
            first_failing = first_failing ? first_failing : i;
            printk("read fault %u/%u: load %d, active %u\n", i, bound, rc, now.data.u32);
        }
    }
    zassert_equal(failing, 0,
                  "%u of %u read positions were hidden or changed the active value "
                  "(first %u)",
                  failing, bound, first_failing);
}

ZTEST(configuration_settings_file, test_read_fault_never_yields_a_stale_value)
{
    crux_config_param_t p = {0};
    unsigned bound, stale = 0, first_stale = 0;

    fresh(&p, &spec, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    set_value(&p, 43);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK); /* two records for one key; 43 is newest */
    remount();
    set_value(&p, 1);
    read_calls = 0;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
    expect_value(&p, 43);
    bound = read_calls;
    for (unsigned i = 1; i <= bound; ++i) {
        remount();
        set_value(&p, 1);
        read_calls = 0;
        fail_read_at = i;
        crux_config_result_t rc = crux_config_load(&p);
        fail_read_at = 0;
        crux_config_value_t now;
        zassert_ok(crux_config_read(&p, &now));
        if (rc == CRUX_CONFIG_OK && now.data.u32 != 43) {
            stale++;
            first_stale = first_stale ? first_stale : i;
            printk("read fault %u/%u: load OK with stale value %u\n", i, bound, now.data.u32);
        }
    }
    zassert_equal(stale, 0, "%u of %u read positions returned an old value as current (first %u)",
                  stale, bound, first_stale);
}

ZTEST(configuration_settings_file, test_interrupted_save_never_yields_a_wrong_value)
{
    crux_config_value_t now;

    for (unsigned position = 1; position <= 3; ++position) {
        crux_config_param_t p = {0};

        fresh(&p, &spec, 42);
        zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
        set_value(&p, 99);
        write_count = 0;
        write_fault = WRITE_ERROR;
        inject_at = position;
        (void)crux_config_save(&p);
        write_fault = WRITE_NONE;
        remount();
        set_value(&p, 1);
        crux_config_result_t rc = crux_config_load(&p);
        zassert_ok(crux_config_read(&p, &now));
        printk("interrupted save at write %u: load %d, active %u\n", position, rc, now.data.u32);
        /* Safety: a load either reports a failure or yields the old or the attempted value. */
        zassert_true(rc != CRUX_CONFIG_OK || now.data.u32 == 42 || now.data.u32 == 99,
                     "position %u produced value %u", position, now.data.u32);
        /* Recovery: a new save after the interruption is stored and loads back. */
        set_value(&p, 7);
        zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
        set_value(&p, 1);
        zassert_equal(crux_config_load(&p), CRUX_CONFIG_OK);
        expect_value(&p, 7);
    }
}

static void reset_file(void)
{
    struct fs_file_t file;

    (void)fs_unlink("/RAM:/filler");
    int rc = fs_unlink("/RAM:/settings/cfg");
    zassert_true(rc == 0 || rc == -ENOENT, "failed to reset the settings file: %d", rc);
    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, "/RAM:/settings/cfg", FS_O_CREATE | FS_O_WRITE));
    zassert_ok(fs_close(&file));
}

/* A settings file that was never created is an empty store, not an I/O error (first boot). */
ZTEST(configuration_settings_file, test_a_missing_settings_file_reads_as_missing)
{
    crux_config_param_t p = {0};

    zassert_ok(fs_unlink("/RAM:/settings/cfg"));
    fresh(&p, &spec, 99);
    crux_config_result_t rc = crux_config_load(&p);
    zassert_equal(rc, CRUX_CONFIG_DEFAULT_MISSING, "a missing settings file gave %d", rc);
    expect_value(&p, 11);
}

/* A compaction that was interrupted between removing the settings file and renaming the new one
 * leaves the data only in the .cmp file. That is a lost file, not a first boot. */
ZTEST(configuration_settings_file, test_missing_file_with_a_compaction_leftover_is_an_error)
{
    crux_config_param_t p = {0};
    struct fs_file_t leftover;

    zassert_ok(fs_unlink("/RAM:/settings/cfg"));
    fs_file_t_init(&leftover);
    zassert_ok(fs_open(&leftover, "/RAM:/settings/cfg.cmp", FS_O_CREATE | FS_O_WRITE));
    zassert_ok(fs_close(&leftover));
    fresh(&p, &spec, 99);
    crux_config_result_t rc = crux_config_load(&p);
    zassert_equal(rc, CRUX_CONFIG_ERR_IO, "a lost file reads as %d", rc);
    zassert_ok(fs_unlink("/RAM:/settings/cfg.cmp"));
}

static int failing_callback(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg,
                            void *param)
{
    ARG_UNUSED(name);
    ARG_UNUSED(len);
    ARG_UNUSED(read_cb);
    ARG_UNUSED(cb_arg);
    (*(unsigned *)param)++;
    return -EIO;
}

/* A callback error must reach the caller of the load instead of being dropped by the backend. */
ZTEST(configuration_settings_file, test_a_callback_error_is_returned_by_the_load)
{
    crux_config_param_t p = {0};
    unsigned calls = 0;

    fresh(&p, &spec, 5);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    int rc = settings_load_subtree_direct("cfg", failing_callback, &calls);
    zassert_true(calls > 0, "the callback never ran");
    zassert_equal(rc, -EIO, "a callback error was returned as %d", rc);
}

/* A read fault during a compaction must not drop another record: either the save fails and the
 * file is kept, or the compaction completes with every record. A compaction rescans the file for
 * every record, so it reads thousands of times; the first positions (the first record's length
 * and name reads) are swept one by one and the rest at an even stride. */
ZTEST(configuration_settings_file, test_compaction_read_fault_never_loses_a_record)
{
    unsigned bound = 0, stride = 1, bad = 0, first_bad = 0, tried = 0;

    for (unsigned phase = 0;; phase++) {
        crux_config_param_t p = {0}, other = {0};

        reset_file();
        fresh(&other, &other_spec, 7);
        zassert_equal(crux_config_save(&other), CRUX_CONFIG_OK);
        zassert_ok(crux_config_init(&p, &spec));
        if (phase == 0) {
            count_compaction_reads = true; /* measure the reads of one compaction */
        } else {
            compaction_read_fault_at = phase;
        }
        crux_config_result_t rc = save_until_compaction(&p);
        crux_config_value_t attempted;
        zassert_ok(crux_config_read(&p, &attempted));
        if (phase == 0) {
            bound = read_calls;
            count_compaction_reads = false;
            zassert_true(bound > 12, "a compaction reads only %u times", bound);
            stride = bound / 40 ? bound / 40 : 1;
            continue;
        }
        if (phase > 12 && phase % stride != 0 && phase != bound) {
            if (phase >= bound) {
                break;
            }
            continue;
        }
        tried++;
        fail_read_at = 0;
        remount();
        set_value(&other, 1);
        set_value(&p, 1);
        crux_config_result_t other_rc = crux_config_load(&other);
        crux_config_result_t p_rc = crux_config_load(&p);
        crux_config_value_t other_now, p_now;
        zassert_ok(crux_config_read(&other, &other_now));
        zassert_ok(crux_config_read(&p, &p_now));
        bool other_kept = other_rc == CRUX_CONFIG_OK && other_now.data.u32 == 7;
        bool save_kept = rc != CRUX_CONFIG_OK ||
                         (p_rc == CRUX_CONFIG_OK && p_now.data.u32 == attempted.data.u32);
        if (!other_kept || !save_kept) {
            bad++;
            first_bad = first_bad ? first_bad : phase;
            printk("compaction read fault %u/%u: save %d, other key load %d value %u, saved key "
                   "load %d value %u\n",
                   phase, bound, rc, other_rc, other_now.data.u32, p_rc, p_now.data.u32);
        }
        if (phase >= bound) {
            break;
        }
    }
    printk("compaction read faults: %u positions tried of %u (stride %u)\n", tried, bound, stride);
    zassert_equal(bad, 0, "%u of %u compaction read positions lost a record (first %u)", bad, tried,
                  first_bad);
}

/* Limit, recorded rather than asserted: an interruption (process kill, power loss) leaves a partial
 * record that no run-time rollback can remove. Three shapes of torn tail are appended to a file
 * holding one good record. The asserted safety property is that a later load never yields a value
 * that was not stored; whether the next save survives is printed. */
static void append_raw(const uint8_t *bytes, size_t length)
{
    struct fs_file_t file;

    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, "/RAM:/settings/cfg", FS_O_RDWR));
    zassert_ok(fs_seek(&file, 0, FS_SEEK_END));
    zassert_equal(fs_write(&file, bytes, length), (ssize_t)length);
    zassert_ok(fs_close(&file));
}

static const struct {
    const char *what;
    uint8_t bytes[16];
    size_t length;
} torn_shapes[] = {
    {"1 byte (partial length field)", {0x01}, 1},
    {"length field and a partial name", {0x14, 0x00, 'c', 'f', 'g', '/', 's', 'p'}, 8},
    {"length field, name and '=' and 3 value bytes",
     {0x14, 0x00, 'c', 'f', 'g', '/', 's', 'p', 'e', 'e', 'd', '=', 1, 2, 7},
     15},
};

ZTEST(configuration_settings_file, test_save_after_a_torn_tail_records_the_outcome)
{
    for (size_t shape = 0; shape < ARRAY_SIZE(torn_shapes); shape++) {
        crux_config_param_t p = {0};
        crux_config_value_t now;

        reset_file();
        fresh(&p, &spec, 42);
        zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
        append_raw(torn_shapes[shape].bytes, torn_shapes[shape].length);
        set_value(&p, 1);
        crux_config_result_t before_save = crux_config_load(&p);
        set_value(&p, 99);
        crux_config_result_t saved = crux_config_save(&p);
        remount();
        set_value(&p, 1);
        crux_config_result_t loaded = crux_config_load(&p);
        zassert_ok(crux_config_read(&p, &now));
        printk("LIMIT torn tail [%s]: load %d; save %d; reload %d value %u (99 %s)\n",
               torn_shapes[shape].what, before_save, saved, loaded, now.data.u32,
               (loaded == CRUX_CONFIG_OK && now.data.u32 == 99) ? "kept" : "lost");
        zassert_true(loaded != CRUX_CONFIG_OK || now.data.u32 == 42 || now.data.u32 == 99,
                     "shape %zu: load yielded %u, which was never stored", shape, now.data.u32);
    }
}

/* Mitigation prototype in the provider copy: read each saved record back and compare. A save that
 * cannot be read back is reported as an error instead of acknowledged and lost. */
ZTEST(configuration_settings_file, test_read_back_turns_a_lost_save_into_an_error)
{
    for (size_t shape = 0; shape < ARRAY_SIZE(torn_shapes); shape++) {
        crux_config_param_t p = {0};

        reset_file();
        fresh(&p, &spec, 42);
        zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
        append_raw(torn_shapes[shape].bytes, torn_shapes[shape].length);
        crux_probe_read_back = true;
        set_value(&p, 99);
        crux_config_result_t rc = crux_config_save(&p);
        crux_probe_read_back = false;
        zassert_equal(rc, CRUX_CONFIG_ERR_IO,
                      "shape %zu [%s]: a save that cannot be read back was "
                      "acknowledged (%d)",
                      shape, torn_shapes[shape].what, rc);
    }
    /* On a healthy file the read-back changes nothing. */
    crux_config_param_t healthy = {0};
    reset_file();
    fresh(&healthy, &spec, 5);
    crux_probe_read_back = true;
    zassert_equal(crux_config_save(&healthy), CRUX_CONFIG_OK);
    crux_probe_read_back = false;
}

ZTEST(configuration_settings_file, test_malformed_records_are_rejected)
{
    uint8_t wire[9] = {1, 2, 7, 0, 44, 0, 0, 0, 0};

    for (size_t size = 4; size <= 9; ++size) {
        crux_config_param_t p = {0};
        if (size == 8) {
            wire[0] = 99;
        }
        zassert_ok(settings_save_one("cfg/speed", wire, size));
        fresh(&p, &spec, 42);
        zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_REJECTED, "size %zu", size);
        expect_value(&p, 11);
    }
}

/* Integrity limit: the file backend stores no checksum. A flip that breaks the record structure is
 * rejected, but a flip inside the value yields another valid value that is accepted. */
ZTEST(configuration_settings_file, test_value_only_corruption_is_not_detected)
{
    static const uint8_t wire[8] = {1, 2, 7, 0, 42, 0, 0, 0};
    uint8_t image[512];
    struct fs_file_t file;
    crux_config_param_t p = {0};
    off_t at = -1;

    fresh(&p, &spec, 42);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_OK);
    fs_file_t_init(&file);
    zassert_ok(fs_open(&file, "/RAM:/settings/cfg", FS_O_RDWR));
    ssize_t length = fs_read(&file, image, sizeof(image));
    zassert_true(length > (ssize_t)sizeof(wire));
    for (ssize_t i = 0; i + (ssize_t)sizeof(wire) <= length; ++i) {
        if (memcmp(&image[i], wire, sizeof(wire)) == 0) {
            at = i;
        }
    }
    zassert_true(at >= 0, "record not found in the settings file");
    for (unsigned field = 0; field < 3; ++field) { /* version, type, value */
        static const unsigned offset[3] = {0, 1, 4};
        uint8_t flipped = (uint8_t)(wire[offset[field]] ^ 1);
        zassert_ok(fs_seek(&file, at + offset[field], FS_SEEK_SET));
        zassert_equal(fs_write(&file, &flipped, 1), 1);
        zassert_ok(fs_sync(&file));
        set_value(&p, 1);
        crux_config_result_t rc = crux_config_load(&p);
        if (field < 2) {
            zassert_equal(rc, CRUX_CONFIG_DEFAULT_REJECTED, "structure flip %u", field);
        } else {
            zassert_equal(rc, CRUX_CONFIG_OK, "a value-only flip is accepted as valid");
            expect_value(&p, 43);
        }
        zassert_ok(fs_seek(&file, at + offset[field], FS_SEEK_SET));
        zassert_equal(fs_write(&file, &wire[offset[field]], 1), 1);
    }
    zassert_ok(fs_close(&file));
}

ZTEST(configuration_settings_file, test_many_saves_keep_every_key_across_compactions)
{
    static const crux_config_spec_t third = {
        .key = "third",
        .schema_version = 7,
        .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 5},
    };
    crux_config_param_t a = {0}, b = {0}, c = {0};
    bool compacted = false;

    zassert_ok(crux_config_init(&a, &spec));
    zassert_ok(crux_config_init(&b, &other_spec));
    zassert_ok(crux_config_init(&c, &third));
    for (uint32_t i = 1; i <= 120; i++) {
        set_value(&a, 1000 + i);
        set_value(&b, 2000 + i);
        set_value(&c, 3000 + i);
        compaction_started = false;
        zassert_equal(crux_config_save(&a), CRUX_CONFIG_OK, "save a %u", i);
        compacted = compacted || compaction_started;
        zassert_equal(crux_config_save(&b), CRUX_CONFIG_OK, "save b %u", i);
        compacted = compacted || compaction_started;
        zassert_equal(crux_config_save(&c), CRUX_CONFIG_OK, "save c %u", i);
        compacted = compacted || compaction_started;
    }
    zassert_true(compacted, "the workload must cross at least one compaction");
    remount();
    set_value(&a, 1);
    set_value(&b, 1);
    set_value(&c, 1);
    zassert_equal(crux_config_load(&a), CRUX_CONFIG_OK);
    zassert_equal(crux_config_load(&b), CRUX_CONFIG_OK);
    zassert_equal(crux_config_load(&c), CRUX_CONFIG_OK);
    expect_value(&a, 1120);
    expect_value(&b, 2120);
    expect_value(&c, 3120);
}

ZTEST_SUITE(configuration_settings_file, NULL, setup, before, NULL, NULL);
