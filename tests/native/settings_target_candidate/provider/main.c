/* SPDX-License-Identifier: Apache-2.0 */
/* Actual simulator-only Crux provider + Settings + NVS; no target I/O. */
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

/* Private reset of upstream initialization state, confined to this test. */
extern bool settings_subsys_initialized;
static struct settings_nvs *backend;
static const struct device *real =
    DEVICE_DT_GET(DT_MTD_FROM_PARTITION(DT_NODELABEL(candidate_nvs)));
static struct device proxy;
static struct flash_driver_api api;
static const struct flash_driver_api *original;
static unsigned reads, writes, erases, fail_read;
static const crux_config_spec_t spec = {.key = "speed",
                                        .schema_version = 7,
                                        .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 11}};
static int fault_read(const struct device *dev, off_t offset, void *data, size_t length) {
    ARG_UNUSED(dev);
    if (++reads == fail_read)
        return -EIO;
    return original->read(real, offset, data, length);
}
static int trace_write(const struct device *dev, off_t offset, const void *data, size_t length) {
    ARG_UNUSED(dev);
    ++writes;
    return original->write(real, offset, data, length);
}
static int trace_erase(const struct device *dev, off_t offset, size_t length) {
    ARG_UNUSED(dev);
    ++erases;
    return original->erase(real, offset, length);
}
static void before(void *unused) {
    ARG_UNUSED(unused);
    original = real->api;
    proxy = *real;
    api = *original;
    api.read = fault_read;
    api.write = trace_write;
    api.erase = trace_erase;
    proxy.api = &api;
    fail_read = reads = writes = erases = 0;
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    /* Explicit simulator-only provisioning; never inferred from missing count. */
    struct nvs_fs provision = {.flash_device = real,
                               .offset = PARTITION_OFFSET(candidate_nvs),
                               .sector_size = 4096,
                               .sector_count = 16};
    uint16_t count = 0x8000;
    zassert_ok(nvs_mount(&provision));
    zassert_equal(nvs_write(&provision, 0x8000, &count, sizeof(count)), sizeof(count));
    settings_subsys_initialized = false;
    zassert_ok(crux_config_zephyr_init());
    void *storage = NULL;
    zassert_ok(settings_storage_get(&storage));
    backend = CONTAINER_OF((struct nvs_fs *)storage, struct settings_nvs, cf_nvs);
    backend->cf_nvs.flash_device = &proxy;
    reads = writes = erases = 0;
}
static void parameter(crux_config_param_t *p) {
    zassert_ok(crux_config_init(p, &spec));
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(p, &v));
}
static void expect_active(crux_config_param_t *p) {
    crux_config_value_t v;
    zassert_ok(crux_config_read(p, &v));
    zassert_equal(v.data.u32, 99);
}
static void expect_init_failure(crux_config_param_t *p) {
    settings_subsys_initialized = false;
    zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_ERR_IO);
    zassert_equal(crux_config_load(p), CRUX_CONFIG_ERR_NOT_READY);
    expect_active(p);
}
ZTEST(settings_target_provider, test_roundtrip_bool_u32) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_DEFAULT_MISSING);
    crux_config_value_t active = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&p, &active));
    zassert_ok(crux_config_save(&p));
    zassert_ok(crux_config_update(&p, &spec.default_value));
    zassert_ok(crux_config_load(&p));
    expect_active(&p);
    crux_config_spec_t bs = {.key = "enabled",
                             .schema_version = 1,
                             .default_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = false}};
    crux_config_value_t v = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    crux_config_param_t b = {0};
    zassert_ok(crux_config_init(&b, &bs));
    zassert_ok(crux_config_update(&b, &v));
    zassert_ok(crux_config_save(&b));
    zassert_ok(crux_config_update(&b, &bs.default_value));
    zassert_ok(crux_config_load(&b));
    zassert_ok(crux_config_read(&b, &v));
    zassert_true(v.data.boolean);
}
ZTEST(settings_target_provider, test_unprovisioned_erased_refused) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    expect_init_failure(&p);
}
ZTEST(settings_target_provider, test_missing_count_refused) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    zassert_ok(nvs_delete(&backend->cf_nvs, 0x8000));
    expect_init_failure(&p);
}
ZTEST(settings_target_provider, test_malformed_count_refused) {
    crux_config_param_t p = {0};
    parameter(&p);
    uint8_t bad = 1;
    zassert_equal(nvs_write(&backend->cf_nvs, 0x8000, &bad, sizeof(bad)), sizeof(bad));
    expect_init_failure(&p);
}
static void orphan(uint16_t id) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    zassert_ok(nvs_delete(&backend->cf_nvs, id));
    reads = writes = erases = 0;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_active(&p);
    zassert_equal(crux_config_save(&p), CRUX_CONFIG_ERR_IO);
    expect_active(&p);
    zassert_equal(writes, 0);
    zassert_equal(erases, 0);
}
ZTEST(settings_target_provider, test_orphan_name_refused_without_repair) {
    orphan(0xc001);
}
ZTEST(settings_target_provider, test_orphan_value_refused_without_repair) {
    orphan(0x8001);
}
ZTEST(settings_target_provider, test_hard_read_is_not_missing) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    reads = writes = erases = 0;
    fail_read = 1;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_active(&p);
    zassert_equal(writes, 0);
    zassert_equal(erases, 0);
    fail_read = 0;
}
ZTEST(settings_target_provider, test_full_record_crc_failure_preserves_active) {
    crux_config_param_t p = {0};
    parameter(&p);
    zassert_ok(crux_config_save(&p));
    uint8_t wire[8] = {1, 2, 7, 0, 99, 0, 0, 0};
    size_t size, matches = 0;
    uint8_t *memory = flash_simulator_get_memory(real, &size);
    zassert_not_null(memory);
    for (size_t i = PARTITION_OFFSET(candidate_nvs);
         i < PARTITION_OFFSET(candidate_nvs) + PARTITION_SIZE(candidate_nvs) - sizeof(wire); ++i) {
        if (!memcmp(memory + i, wire, sizeof(wire))) {
            memory[i + 4] ^= 1;
            matches++;
        }
    }
    zassert_equal(matches, 1);
    reads = writes = erases = 0;
    zassert_equal(crux_config_load(&p), CRUX_CONFIG_ERR_IO);
    expect_active(&p);
    zassert_equal(writes, 0);
    zassert_equal(erases, 0);
}
ZTEST_SUITE(settings_target_provider, NULL, NULL, before, NULL, NULL);
