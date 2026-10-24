/* SPDX-License-Identifier: Apache-2.0 */
#include "configuration_settings.h"
#include <crux/configuration.h>
#include <errno.h>
#include <settings/settings_nvs.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

static struct settings_nvs *backend;
static const struct device *real;
static struct device proxy;
static struct flash_driver_api api;
static const struct flash_driver_api *original;
static unsigned reads, writes, erases, fail_read, fail_write;
static off_t failed_offset;
static int write_error = -EIO;
static bool short_record;
static int fault_read(const struct device *dev, off_t offset, void *data, size_t length) {
    ARG_UNUSED(dev);
    if (++reads == fail_read) {
        failed_offset = offset;
        return -EIO;
    }
    return original->read(real, offset, data, length);
}
static int fault_write(const struct device *dev, off_t offset, const void *data, size_t length) {
    ARG_UNUSED(dev);
    if (++writes == fail_write) {
        return write_error;
    }
    return original->write(real, offset, data, length);
}
static int trace_erase(const struct device *dev, off_t offset, size_t length) {
    ARG_UNUSED(dev);
    erases++;
    return original->erase(real, offset, length);
}
static void remount(void) {
    memset(&backend->cf_nvs, 0, sizeof(backend->cf_nvs));
    backend->cf_nvs.offset = PARTITION_OFFSET(candidate_nvs);
    backend->cf_nvs.sector_size = 4096;
    backend->cf_nvs.sector_count = 16;
    backend->last_name_id = 0;
    zassert_ok(settings_nvs_backend_init(backend));
    backend->cf_nvs.flash_device = &proxy;
}
/* A single test source supplies a callback short count, which the NVS API
 * itself cannot produce: NVS returns advertised length or an errno. All other
 * cases still use only the actual Settings/NVS/Flash store. */
static ssize_t short_read(void *arg, void *data, size_t length) {
    ARG_UNUSED(arg);
    memset(data, 0, length);
    return (ssize_t)length - 1;
}
static int short_load(struct settings_store *store, const struct settings_load_arg *arg) {
    ARG_UNUSED(store);
    return short_record ? settings_call_set_handler("cfg/short", 8, short_read, NULL, arg) : 0;
}
static const struct settings_store_itf short_itf = {.csi_load = short_load};
static struct settings_store short_store = {.cs_itf = &short_itf};
static void *setup(void) {
    zassert_ok(crux_config_zephyr_init());
    void *storage = NULL;
    zassert_ok(settings_storage_get(&storage));
    backend = CONTAINER_OF((struct nvs_fs *)storage, struct settings_nvs, cf_nvs);
    real = backend->flash_dev;
    original = real->api;
    proxy = *real;
    api = *original;
    api.read = fault_read;
    api.write = fault_write;
    api.erase = trace_erase;
    proxy.api = &api;
    settings_src_register(&short_store);
    return NULL;
}
static void before(void *unused) {
    ARG_UNUSED(unused);
    reads = writes = erases = fail_read = fail_write = 0;
    failed_offset = -1;
    write_error = -EIO;
    short_record = false;
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    remount();
}
static const crux_config_spec_t spec = {.key = "speed",
                                        .schema_version = 7,
                                        .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 11}};
static void parameter(crux_config_param_t *p) {
    zassert_ok(crux_config_init(p, &spec));
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = 42};
    zassert_ok(crux_config_update(p, &v));
}
static void expect_value(crux_config_param_t *p, uint32_t expected) {
    crux_config_value_t v;
    zassert_ok(crux_config_read(p, &v));
    zassert_equal(v.data.u32, expected);
}
ZTEST(configuration_settings, test_callback_short_count_preserves_active) {
    crux_config_spec_t short_spec = spec;
    short_spec.key = "short";
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &short_spec));
    crux_config_value_t active = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&p, &active));
    short_record = true;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_value(&p, 99);
    short_record = false;
}
ZTEST(configuration_settings, test_staging_limit_boundary) {
    uint8_t wire[257] = {1, 2, 7, 0, 42};
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(settings_save_one("cfg/speed", wire, 256));
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_REJECTED);
    expect_value(&p, 11);
    crux_config_value_t active = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&p, &active));
    zassert_ok(settings_save_one("cfg/speed", wire, 257));
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_value(&p, 99);
}
ZTEST(configuration_settings, test_save_no_space_mapping) {
    crux_config_param_t p = {0};
    parameter(&p);
    writes = 0;
    fail_write = 1;
    write_error = -ENOSPC;
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_ERR_NO_SPACE);
    expect_value(&p, 42);
    write_error = -EIO;
    fail_write = 0;
}
ZTEST(configuration_settings, test_mount_all_open_sector_comparison_read_error) {
    /* Empty sectors reach the comparison preceding the last closing ATE,
     * after one successful closing-ATE comparison per sector. */
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    struct nvs_fs fs = {
        .flash_device = &proxy,
        .offset = PARTITION_OFFSET(candidate_nvs),
        .sector_size = 4096,
        .sector_count = 16,
    };
    reads = writes = erases = 0;
    fail_read = fs.sector_count + 1;
    int rc = nvs_mount(&fs);
    printk("CONFIGURATION_MOUNT_FAULT rc=%d reads=%u writes=%u erases=%u offset=%ld\n", rc, reads,
           writes, erases, (long)failed_offset);
    zassert_equal(failed_offset, fs.offset + fs.sector_size * fs.sector_count - 16);
    zassert_equal(rc, -EIO, "late empty-sector comparison must preserve the read error");
    zassert_equal(reads, fail_read, "no reads after failed comparison");
    zassert_equal(writes, 0, "no writes after failed comparison");
    zassert_equal(erases, 0, "no erases after failed comparison");
    zassert_false(fs.ready);
    fail_read = 0;
}
ZTEST(configuration_settings, test_roundtrip_and_missing) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_MISSING);
    expect_value(&p, 11);
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = 42};
    zassert_ok(crux_config_update(&p, &v));
    zassert_ok(crux_config_save(&p));
    remount();
    zassert_ok(crux_config_update(&p, &spec.default_value));
    zassert_ok(crux_config_load(&p));
    expect_value(&p, 42);
    crux_config_spec_t bspec = {.key = "enabled",
                                .schema_version = 1,
                                .default_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = false}};
    crux_config_param_t b = {0};
    crux_config_value_t bv = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    zassert_ok(crux_config_init(&b, &bspec));
    zassert_ok(crux_config_update(&b, &bv));
    zassert_ok(crux_config_save(&b));
    remount();
    zassert_ok(crux_config_update(&b, &bspec.default_value));
    zassert_ok(crux_config_load(&b));
    zassert_ok(crux_config_read(&b, &bv));
    zassert_true(bv.data.boolean);
}
ZTEST(configuration_settings, test_traversal_error_is_not_missing) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    crux_config_spec_t absent_spec = spec;
    absent_spec.key = "absent";
    crux_config_param_t absent = {0};
    zassert_ok(crux_config_init(&absent, &absent_spec));
    crux_config_value_t active = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&absent, &active));
    reads = 0;
    fail_read = 1;
    zassert_equal(crux_config_load(&absent), CRUX_CONFIG_ERR_IO);
    expect_value(&absent, 99);
    fail_read = 0;
    zassert_equal(crux_config_load(&absent), CRUX_CONFIG_DEFAULT_MISSING);
    expect_value(&absent, 11);
}
ZTEST(configuration_settings, test_each_load_read_fault_preserves_active) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    uint8_t unrelated[5] = {1, 1, 1, 0, 1};
    zassert_ok(settings_save_one("cfg/other", unrelated, sizeof(unrelated)));
    crux_config_value_t active = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    remount();
    reads = 0;
    zassert_ok(crux_config_load(&p));
    unsigned bound = reads;
    zassert_true(bound > 0 && bound < 128);
    for (unsigned i = 1; i <= bound; ++i) {
        fail_read = 0;
        remount();
        zassert_ok(crux_config_update(&p, &active));
        reads = 0;
        fail_read = i;
        zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO, "read %u/%u", i, bound);
        expect_value(&p, 99);
    }
    fail_read = 0;
    printk("CONFIGURATION_READ_FAULTS positions=%u active_preserved=99\n", bound);
}
ZTEST(configuration_settings, test_save_failure_and_interrupted_reinit) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&p, &v));
    writes = 0;
    fail_write = 2; /* payload succeeds, its ATE write fails */
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_ERR_IO);
    zassert_equal(writes, 2);
    expect_value(&p, 99);
    fail_write = 0;
    remount();
    zassert_ok(crux_config_load(&p));
    expect_value(&p, 42);
    zassert_ok(crux_config_update(&p, &v));
    zassert_ok(crux_config_save(&p));
    remount();
    zassert_ok(crux_config_load(&p));
    expect_value(&p, 99);
}
ZTEST(configuration_settings, test_malformed_records) {
    uint8_t wire[9] = {1, 2, 7, 0, 44, 0, 0, 0, 0};
    for (size_t size = 4; size <= 9; ++size) {
        if (size == 8) {
            wire[0] = 99;
        }
        zassert_ok(settings_save_one("cfg/speed", wire, size));
        crux_config_param_t p = {0};
        parameter(&p);
        zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_REJECTED);
        expect_value(&p, 11);
    }
}
ZTEST(configuration_settings, test_full_record_crc_failure_preserves_active) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    uint8_t wire[8] = {1, 2, 7, 0, 42, 0, 0, 0};
    size_t size, matches = 0;
    uint8_t *memory = flash_simulator_get_memory(real, &size);
    zassert_not_null(memory);
    for (size_t i = PARTITION_OFFSET(candidate_nvs);
         i < PARTITION_OFFSET(candidate_nvs) + PARTITION_SIZE(candidate_nvs) - sizeof(wire); ++i) {
        if (memcmp(memory + i, wire, sizeof(wire)) == 0) {
            memory[i + 4] ^= 1;
            matches++;
        }
    }
    zassert_equal(matches, 1);
    for (unsigned phase = 0; phase < 2; ++phase) {
        if (phase) {
            remount();
        }
        zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
        expect_value(&p, 42);
    }
}
ZTEST_SUITE(configuration_settings, NULL, setup, before, NULL, NULL);
