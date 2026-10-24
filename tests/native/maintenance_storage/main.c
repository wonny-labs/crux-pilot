/* SPDX-License-Identifier: Apache-2.0 */
/* Actual File/LittleFS + Configuration/Settings/NVS, exclusive software fixture.
 * Breaks caught: omitted finish gate, live-handle escape, lost failure latch,
 * portable precedence regression, or unsupported healthy-remount/GC assumptions. */
#include "configuration_settings.h"
#include "file_native.h"
#include "gate.h"
#include "maintenance.h"
#include <crux/configuration.h>
#include <crux/file.h>
#include <errno.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kvss/nvs.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
extern bool settings_subsys_initialized;
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t mount = {.type = FS_LITTLEFS,
                                  .fs_data = &storage,
                                  .storage_dev = (void *)PARTITION_ID(file_test_partition),
                                  .mnt_point = "/files",
                                  .flags = FS_MOUNT_FLAG_NO_FORMAT};
static bool bound;
static crux_file_t *file;
static int close_error, sync_error, write_error;
static unsigned settings_dispatches;
extern int __real_settings_subsys_init(void);
int __wrap_settings_subsys_init(void) {
    ++settings_dispatches;
    return __real_settings_subsys_init();
}
extern int __real_settings_save_one(const char *, const void *, size_t);
int __wrap_settings_save_one(const char *name, const void *value, size_t length) {
    ++settings_dispatches;
    return __real_settings_save_one(name, value, length);
}
extern int __real_settings_load_subtree_direct(const char *, settings_load_direct_cb, void *);
int __wrap_settings_load_subtree_direct(const char *subtree, settings_load_direct_cb cb,
                                        void *arg) {
    ++settings_dispatches;
    return __real_settings_load_subtree_direct(subtree, cb, arg);
}
extern int __real_fs_close(struct fs_file_t *);
int __wrap_fs_close(struct fs_file_t *f) {
    int rc = __real_fs_close(f); /* real native release, then injected acknowledgement error */
    return close_error ? close_error : rc;
}
extern int __real_fs_sync(struct fs_file_t *);
int __wrap_fs_sync(struct fs_file_t *f) {
    return sync_error ? sync_error : __real_fs_sync(f);
}
extern ssize_t __real_fs_write(struct fs_file_t *, const void *, size_t);
ssize_t __wrap_fs_write(struct fs_file_t *f, const void *bytes, size_t length) {
    return write_error ? write_error : __real_fs_write(f, bytes, length);
}
static bool valid_cycles(const crux_config_value_t *v, void *context) {
    ARG_UNUSED(context);
    return v->data.u32 >= 1 && v->data.u32 <= 100;
}
static const crux_config_spec_t cycles_spec = {
    .key = "report_every_cycles",
    .schema_version = 1,
    .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 1},
    .validate = valid_cycles};
static const crux_config_spec_t diagnostics_spec = {
    .key = "diagnostics_enabled",
    .schema_version = 1,
    .default_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = false}};
static crux_config_param_t cycles, diagnostics;
static void parameters(void) {
    cycles = (crux_config_param_t){0};
    diagnostics = (crux_config_param_t){0};
    zassert_ok(crux_config_init(&cycles, &cycles_spec));
    zassert_ok(crux_config_init(&diagnostics, &diagnostics_spec));
}
static void values(uint32_t c, bool d) {
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = c};
    zassert_ok(crux_config_update(&cycles, &v));
    v = (crux_config_value_t){.type = CRUX_CONFIG_BOOL, .data.boolean = d};
    zassert_ok(crux_config_update(&diagnostics, &v));
}
static void read_values(uint32_t c, bool d) {
    crux_config_value_t v;
    zassert_ok(crux_config_read(&cycles, &v));
    zassert_equal(v.type, CRUX_CONFIG_U32);
    zassert_equal(v.data.u32, c);
    zassert_ok(crux_config_read(&diagnostics, &v));
    zassert_equal(v.type, CRUX_CONFIG_BOOL);
    zassert_equal(v.data.boolean, d);
}
static void config_newboot(void) {
    gate_reset();
    settings_subsys_initialized = false;
    maintenance_newboot();
    zassert_ok(crux_config_zephyr_init());
    printk("HEALTHY_MOUNT reads=%u writes=%u erases=%u denied=%u\n", gate_reads, gate_writes,
           gate_erases, gate_denied);
    zassert_equal(gate_denied, 0);
    parameters();
}
static void before(void *unused) {
    ARG_UNUSED(unused);
    close_error = sync_error = write_error = 0;
    file = NULL;
    /* Explicit simulator-only provisioning, never an init/mount fallback. */
    const struct device *dev = PARTITION_DEVICE(candidate_nvs);
    zassert_ok(flash_erase(dev, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    struct nvs_fs provision = {.flash_device = dev,
                               .offset = PARTITION_OFFSET(candidate_nvs),
                               .sector_size = 4096,
                               .sector_count = 16};
    zassert_ok(nvs_mount(&provision));
    uint16_t count = 0x8000;
    zassert_equal(nvs_write(&provision, 0x8000, &count, sizeof(count)), sizeof(count));
    maintenance_provisioning(true);
    int formatted = fs_mkfs(FS_LITTLEFS, (uintptr_t)mount.storage_dev, NULL, 0);
    maintenance_provisioning(false);
    zassert_ok(formatted);
    zassert_ok(fs_mount(&mount));
    if (!bound) {
        zassert_ok(crux_file_zephyr_bind("/files"));
        bound = true;
    }
    config_newboot();
}
static void after(void *unused) {
    ARG_UNUSED(unused);
    close_error = sync_error = write_error = 0;
    if (file)
        (void)crux_file_close(&file);
    zassert_ok(fs_unmount(&mount));
}
static void write_file(void) {
    size_t n;
    zassert_ok(crux_file_open("record", CRUX_FILE_CREATE_REPLACE, &file));
    zassert_ok(crux_file_write(file, "maintenance", 11, &n));
    zassert_equal(n, 11);
    zassert_ok(crux_file_sync(file));
    zassert_ok(crux_file_close(&file));
}
static void read_file(void) {
    char bytes[16] = {0};
    size_t n;
    zassert_ok(crux_file_open("record", CRUX_FILE_READ, &file));
    zassert_ok(crux_file_read(file, bytes, sizeof(bytes), &n));
    zassert_equal(n, 11);
    zassert_mem_equal(bytes, "maintenance", 11);
    zassert_ok(crux_file_close(&file));
}
static void config_closed(void) {
    unsigned r = gate_reads, w = gate_writes, e = gate_erases, denied = gate_denied;
    unsigned native = settings_dispatches;
    crux_config_result_t init = crux_config_zephyr_init();
    crux_config_result_t load = crux_config_load(&cycles);
    crux_config_result_t save = crux_config_save(&cycles);
    printk(
        "AFTER_FINISH config init=%d load=%d save=%d native_delta=%u/%u/%u settings_dispatch=%u\n",
        init, load, save, gate_reads - r, gate_writes - w, gate_erases - e,
        settings_dispatches - native);
    zassert_equal(init, CRUX_CONFIG_ERR_NOT_READY);
    zassert_equal(load, CRUX_CONFIG_ERR_NOT_READY);
    zassert_equal(save, CRUX_CONFIG_ERR_NOT_READY);
    zassert_equal(gate_reads, r);
    zassert_equal(gate_writes, w);
    zassert_equal(gate_erases, e);
    zassert_equal(gate_denied, denied);
    zassert_equal(settings_dispatches, native);
}
ZTEST(maintenance_storage, test_four_saves_remount_exact_then_finish) {
    write_file();
    read_file();
    unsigned reads = gate_reads, writes = gate_writes, erases = gate_erases;
    values(10, true);
    zassert_ok(crux_config_save(&cycles));
    zassert_ok(crux_config_save(&diagnostics));
    values(73, false);
    zassert_ok(crux_config_save(&cycles));
    zassert_ok(crux_config_save(&diagnostics));
    printk("FOUR_SAVES native reads=%u writes=%u erases=%u\n", gate_reads - reads,
           gate_writes - writes, gate_erases - erases);
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    config_newboot(); /* normal backend remount; no erase or provisioning */
    read_file();
    zassert_ok(crux_config_load(&cycles));
    zassert_ok(crux_config_load(&diagnostics));
    read_values(73, false);
    zassert_true(maintenance_finish());
    unsigned dispatched = maintenance_file_dispatches;
    crux_file_result_t opened = crux_file_open("record", CRUX_FILE_READ, &file);
    if (file)
        zassert_ok(crux_file_close(&file)); /* keep negative control safe to continue */
    crux_file_result_t removed = crux_file_remove("record");
    printk("AFTER_FINISH file open=%d remove=%d native_delta=%u\n", opened, removed,
           maintenance_file_dispatches - dispatched);
    zassert_equal(opened, CRUX_FILE_ERR_NOT_READY);
    zassert_equal(removed, CRUX_FILE_ERR_NOT_READY);
    zassert_equal(maintenance_file_dispatches, dispatched);
    config_closed();
    parameters(); /* RAM init/update/read stay available with persistence closed */
    values(29, true);
    read_values(29, true);
    zassert_true(maintenance_finish());
    config_closed();
}
ZTEST(maintenance_storage, test_config_finish_stays_closed) {
    values(42, true);
    zassert_ok(crux_config_save(&cycles));
    zassert_true(maintenance_finish());
    config_closed();
    parameters();
    values(100, false);
    read_values(100, false);
    crux_config_value_t bad = {.type = CRUX_CONFIG_U32, .data.u32 = 101};
    zassert_equal(crux_config_update(&cycles, &bad), CRUX_CONFIG_ERR_REJECTED);
    read_values(100, false);
}
ZTEST(maintenance_storage, test_open_handles_refuse_finish_until_all_closed) {
    crux_file_t *second = NULL;
    zassert_ok(crux_file_open("one", CRUX_FILE_CREATE_REPLACE, &file));
    zassert_ok(crux_file_open("two", CRUX_FILE_CREATE_REPLACE, &second));
    bool live_refused = !maintenance_finish();
    zassert_ok(crux_file_close(&file));
    bool second_refused = !maintenance_finish();
    zassert_ok(crux_file_close(&second));
    zassert_true(live_refused);
    zassert_true(second_refused);
    zassert_true(maintenance_finish());
    config_closed();
}
ZTEST(maintenance_storage, test_close_error_consumes_but_refuses_safe_finish) {
    write_file();
    zassert_ok(crux_file_open("record", CRUX_FILE_READ, &file));
    close_error = -12345; /* unknown native error retains existing IO mapping */
    zassert_equal(crux_file_close(&file), CRUX_FILE_ERR_IO);
    zassert_is_null(file);
    close_error = 0;
    zassert_equal(crux_file_close(&file), CRUX_FILE_ERR_INVALID_STATE);
    zassert_false(maintenance_finish());
    zassert_false(maintenance_finish());
    zassert_equal(crux_file_open("record", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
    config_closed();
}
static crux_file_result_t consume_without_mutation(void) {
    unsigned writes = maintenance_media_writes, erases = maintenance_media_erases;
    unsigned denied = maintenance_media_denied;
    crux_file_result_t rc = crux_file_close(&file);
    printk("UNCERTAIN_CLOSE result=%d media_writes=%u media_erases=%u denied=%u\n", rc,
           maintenance_media_writes - writes, maintenance_media_erases - erases,
           maintenance_media_denied - denied);
    zassert_is_null(file);
    zassert_equal(crux_file_close(&file), CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(maintenance_media_writes, writes);
    zassert_equal(maintenance_media_erases, erases);
    if (rc == CRUX_FILE_ERR_IO)
        zassert_true(maintenance_media_denied > denied);
    return rc;
}
ZTEST(maintenance_storage, test_sync_error_not_rollback_or_safe_finish) {
    size_t n;
    zassert_ok(crux_file_open("uncertain", CRUX_FILE_CREATE_REPLACE, &file));
    zassert_ok(crux_file_write(file, "bytes", 5, &n));
    sync_error = -ENOSPC;
    zassert_equal(crux_file_sync(file), CRUX_FILE_ERR_NO_SPACE);
    sync_error = 0;
    zassert_equal(consume_without_mutation(), CRUX_FILE_ERR_IO);
    zassert_false(maintenance_finish());
    config_closed();
}
ZTEST(maintenance_storage, test_portable_argument_stale_and_zero_io_precedence) {
    size_t n = 99;
    zassert_ok(crux_file_open("zero", CRUX_FILE_CREATE_REPLACE, &file));
    unsigned dispatched = maintenance_file_dispatches;
    zassert_ok(crux_file_read(file, NULL, 0, &n));
    zassert_equal(n, 0);
    zassert_ok(crux_file_write(file, NULL, 0, &n));
    zassert_equal(maintenance_file_dispatches, dispatched);
    crux_file_t *stale = file;
    zassert_ok(crux_file_close(&file));
    zassert_true(maintenance_finish());
    dispatched = maintenance_file_dispatches;
    zassert_equal(crux_file_open("bad/name", CRUX_FILE_READ, &file),
                  CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_remove(".."), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_read(stale, NULL, 0, &n), CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(crux_file_write(stale, NULL, 1, &n), CRUX_FILE_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_file_seek(stale, 0), CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(crux_file_sync(stale), CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(crux_file_close(&stale), CRUX_FILE_ERR_INVALID_STATE);
    zassert_equal(maintenance_file_dispatches, dispatched);
}
ZTEST(maintenance_storage, test_normal_nvs_gc_then_readback_and_remount) {
    unsigned before_erase = gate_erases, updates = 0;
    for (unsigned i = 0; i < 12000 && gate_erases == before_erase; ++i) {
        values(1 + (i % 100), (i % 2) == 0);
        zassert_ok(crux_config_save(&cycles));
        zassert_ok(crux_config_save(&diagnostics));
        updates = i + 1;
    }
    printk("NORMAL_GC bound=12000 pair_updates=%u saves=%u reads=%u writes=%u erases=%u\n", updates,
           2 * updates, gate_reads, gate_writes, gate_erases - before_erase);
    zassert_true(gate_erases > before_erase, "bounded actual-engine GC erase required");
    uint32_t c = 1 + ((updates - 1) % 100);
    bool d = ((updates - 1) % 2) == 0;
    zassert_ok(crux_config_load(&cycles));
    zassert_ok(crux_config_load(&diagnostics));
    read_values(c, d);
    config_newboot();
    zassert_ok(crux_config_load(&cycles));
    zassert_ok(crux_config_load(&diagnostics));
    read_values(c, d);
    zassert_true(maintenance_finish());
}
ZTEST(maintenance_storage, test_config_fault_refuses_finish_and_closes_file) {
    size_t n;
    zassert_ok(crux_file_open("dirty", CRUX_FILE_CREATE_REPLACE, &file));
    zassert_ok(crux_file_write(file, "pending", 7, &n));
    gate_fail_read = gate_reads + 1;
    zassert_equal(crux_config_save(&cycles), CRUX_CONFIG_ERR_IO);
    gate_fail_read = 0;
    unsigned dispatched = maintenance_file_dispatches;
    crux_file_t *rejected = NULL;
    crux_file_result_t opened = crux_file_open("blocked", CRUX_FILE_CREATE_REPLACE, &rejected);
    crux_file_result_t removed = crux_file_remove("absent");
    printk("CONFIG_FAULT file open=%d remove=%d native_delta=%u\n", opened, removed,
           maintenance_file_dispatches - dispatched);
    zassert_equal(opened, CRUX_FILE_ERR_NOT_READY);
    zassert_equal(removed, CRUX_FILE_ERR_NOT_READY);
    zassert_equal(maintenance_file_dispatches, dispatched);
    zassert_false(maintenance_finish());
    zassert_equal(consume_without_mutation(), CRUX_FILE_ERR_IO);
    zassert_false(maintenance_finish());
    zassert_equal(crux_file_open("blocked", CRUX_FILE_CREATE_REPLACE, &file),
                  CRUX_FILE_ERR_NOT_READY);
    config_closed();
}
ZTEST(maintenance_storage, test_write_io_closes_persistence_but_allows_consuming_cleanup) {
    size_t n = 99;
    zassert_ok(crux_file_open("write-fault", CRUX_FILE_CREATE_REPLACE, &file));
    write_error = -EIO;
    zassert_equal(crux_file_write(file, "bytes", 5, &n), CRUX_FILE_ERR_IO);
    zassert_equal(n, 0);
    write_error = 0;
    zassert_false(maintenance_finish()); /* open handle still needs consuming cleanup */
    config_closed();
    crux_file_result_t closed = consume_without_mutation();
    zassert_true(closed == CRUX_FILE_OK || closed == CRUX_FILE_ERR_IO);
    zassert_false(maintenance_finish());
    zassert_equal(crux_file_open("write-fault", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
}
/* The same fixed public-API trial is retained in the target candidate. */
extern int crux_candidate_values_trial(void);
extern int crux_candidate_values_readback(void);
ZTEST(maintenance_storage, test_rt1060_fixed_trial_and_healthy_remount) {
    zassert_ok(crux_candidate_values_trial());
    zassert_ok(fs_unmount(&mount));
    zassert_ok(fs_mount(&mount));
    config_newboot();
    zassert_ok(crux_config_load(&cycles));
    zassert_ok(crux_config_load(&diagnostics));
    read_values(7, true);
    zassert_ok(crux_candidate_values_readback());
    zassert_true(maintenance_finish());
    config_closed();
}
ZTEST_SUITE(maintenance_storage, NULL, NULL, before, after, NULL);
