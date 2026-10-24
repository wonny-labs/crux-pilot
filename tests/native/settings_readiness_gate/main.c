/* SPDX-License-Identifier: Apache-2.0 */
/* Actual Configuration/Settings/NVS/Flashsim composition, software only. */
#include "configuration_settings.h"
#include "gate.h"
#include <crux/configuration.h>
#include <errno.h>
#include <settings/settings_nvs.h>
#include <string.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/flash/flash_simulator.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
extern bool settings_subsys_initialized;
static const struct device *real =
    DEVICE_DT_GET(DT_MTD_FROM_PARTITION(DT_NODELABEL(candidate_nvs)));
static struct nvs_fs provision;
static bool mask_init_error, mask_save_error, mask_load_error;
extern int __real_settings_subsys_init(void);
int __wrap_settings_subsys_init(void) {
    int rc = __real_settings_subsys_init();
    if (mask_init_error) {
        printk("MASKED actual_settings_init=%d denied=%u\n", rc, gate_denied);
        return 0;
    }
    return rc;
}
extern int __real_settings_save_one(const char *, const void *, size_t);
int __wrap_settings_save_one(const char *name, const void *value, size_t length) {
    int rc = __real_settings_save_one(name, value, length);
    if (mask_save_error) {
        printk("MASKED actual_settings_save=%d\n", rc);
        return 0;
    }
    return rc;
}
extern int __real_settings_load_subtree_direct(const char *, settings_load_direct_cb, void *);
int __wrap_settings_load_subtree_direct(const char *subtree, settings_load_direct_cb cb,
                                        void *arg) {
    int rc = __real_settings_load_subtree_direct(subtree, cb, arg);
    if (mask_load_error) {
        printk("MASKED actual_settings_load=%d\n", rc);
        return 0;
    }
    return rc;
}
static crux_config_param_t speed;
static const crux_config_spec_t spec = {.key = "speed",
                                        .schema_version = 7,
                                        .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 11}};
static void counts(void) {
    gate_reads = gate_writes = gate_erases = gate_denied = 0;
}
static void before(void *unused) {
    ARG_UNUSED(unused);
    mask_init_error = mask_save_error = mask_load_error = false;
    gate_reset();
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    provision = (struct nvs_fs){.flash_device = real,
                                .offset = PARTITION_OFFSET(candidate_nvs),
                                .sector_size = 4096,
                                .sector_count = 16};
    /* Explicit test provisioning is outside gate counters and never a runtime fallback. */
    zassert_ok(nvs_mount(&provision));
    uint16_t count = 0x8000;
    zassert_equal(nvs_write(&provision, 0x8000, &count, sizeof(count)), sizeof(count));
    settings_subsys_initialized = false;
    zassert_ok(crux_config_zephyr_init());
    speed = (crux_config_param_t){0};
    zassert_ok(crux_config_init(&speed, &spec));
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&speed, &v));
    counts();
}
static void active(void) {
    crux_config_value_t v;
    zassert_ok(crux_config_read(&speed, &v));
    zassert_equal(v.data.u32, 99);
}
static void no_dispatch(void) {
    printk("GATE reads=%u dispatched_write=%u dispatched_erase=%u denied=%u\n", gate_reads,
           gate_writes, gate_erases, gate_denied);
    zassert_equal(gate_writes, 0);
    zassert_equal(gate_erases, 0);
}
static void faulted(void) {
    zassert_equal(crux_config_save(&speed), CRUX_CONFIG_ERR_NOT_READY);
    zassert_equal(crux_config_load(&speed), CRUX_CONFIG_ERR_NOT_READY);
    active();
    no_dispatch();
}
static void reinit_fails(void) {
    gate_reset();
    settings_subsys_initialized = false;
    crux_config_result_t rc = crux_config_zephyr_init();
    printk("INIT result=%d\n", rc);
    no_dispatch();
    zassert_not_equal(rc, CRUX_CONFIG_OK);
    faulted();
}
static void reinit_unchanged(off_t offset) {
    static uint8_t snapshot[4096], observed[4096];
    zassert_ok(flash_read(real, offset, snapshot, sizeof(snapshot)));
    gate_reset();
    settings_subsys_initialized = false;
    crux_config_result_t rc = crux_config_zephyr_init();
    zassert_ok(flash_read(real, offset, observed, sizeof(observed)));
    printk("INIT result=%d sector_bytes_unchanged=%d\n", rc,
           memcmp(snapshot, observed, sizeof(snapshot)) == 0);
    zassert_mem_equal(snapshot, observed, sizeof(snapshot));
    no_dispatch();
    zassert_not_equal(rc, CRUX_CONFIG_OK);
    faulted();
}
static void corrupt(void) {
    const uint8_t wire[8] = {1, 2, 7, 0, 99, 0, 0, 0};
    size_t n, matches = 0;
    uint8_t *memory = flash_simulator_get_memory(real, &n);
    zassert_not_null(memory);
    for (size_t i = PARTITION_OFFSET(candidate_nvs);
         i + sizeof(wire) < PARTITION_OFFSET(candidate_nvs) + PARTITION_SIZE(candidate_nvs); i++) {
        if (!memcmp(memory + i, wire, sizeof(wire))) {
            memory[i + 4] ^= 1;
            matches++;
        }
    }
    zassert_equal(matches, 1);
}
ZTEST(readiness_gate, test_healthy_bool_u32_missing_and_writes) {
    zassert_equal(crux_config_load(&speed), CRUX_CONFIG_DEFAULT_MISSING);
    crux_config_value_t v = {.type = CRUX_CONFIG_U32, .data.u32 = 99};
    zassert_ok(crux_config_update(&speed, &v));
    zassert_ok(crux_config_save(&speed));
    zassert_true(gate_writes > 0);
    zassert_ok(crux_config_load(&speed));
    active();
    crux_config_spec_t bs = {.key = "enabled",
                             .schema_version = 1,
                             .default_value = {.type = CRUX_CONFIG_BOOL, .data.boolean = false}};
    crux_config_param_t b = {0};
    zassert_ok(crux_config_init(&b, &bs));
    v = (crux_config_value_t){.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    zassert_ok(crux_config_update(&b, &v));
    zassert_ok(crux_config_save(&b));
    zassert_ok(crux_config_update(&b, &bs.default_value));
    zassert_ok(crux_config_load(&b));
    zassert_ok(crux_config_read(&b, &v));
    zassert_true(v.data.boolean);
}
ZTEST(readiness_gate, test_fresh_mount_mutation_denied_before_dispatch) {
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    reinit_unchanged(PARTITION_OFFSET(candidate_nvs));
    zassert_true(gate_denied > 0);
}
ZTEST(readiness_gate, test_mount_repair_denied_and_latched) {
    /* Program unused next sector: actual NVS startup attempts to erase it. */
    uint8_t marker = 0;
    zassert_ok(flash_write(real, PARTITION_OFFSET(candidate_nvs) + 4096, &marker, 1));
    reinit_unchanged(PARTITION_OFFSET(candidate_nvs) + 4096);
    zassert_true(gate_denied > 0);
}
ZTEST(readiness_gate, test_corrupt_current_key_cannot_be_overwritten) {
    zassert_ok(crux_config_save(&speed));
    corrupt();
    counts();
    crux_config_result_t rc = crux_config_save(&speed);
    printk("SAVE current result=%d\n", rc);
    no_dispatch();
    zassert_equal(rc, CRUX_CONFIG_ERR_IO);
    faulted();
}
ZTEST(readiness_gate, test_corrupt_other_key_cannot_be_overwritten) {
    zassert_ok(crux_config_save(&speed));
    corrupt();
    counts();
    crux_config_spec_t other = spec;
    other.key = "other";
    crux_config_param_t p = {0};
    zassert_ok(crux_config_init(&p, &other));
    crux_config_result_t rc = crux_config_save(&p);
    printk("SAVE other result=%d\n", rc);
    no_dispatch();
    zassert_equal(rc, CRUX_CONFIG_ERR_IO);
    faulted();
}
ZTEST(readiness_gate, test_corrupt_record_blocks_startup_publication) {
    zassert_ok(crux_config_save(&speed));
    corrupt();
    reinit_fails();
}
ZTEST(readiness_gate, test_hard_read_is_sticky_not_missing) {
    gate_fail_read = 1;
    zassert_equal(crux_config_save(&speed), CRUX_CONFIG_ERR_IO);
    gate_fail_read = 0;
    faulted();
}
ZTEST(readiness_gate, test_oversize_blocks_ready) {
    uint8_t large[257] = {1};
    zassert_ok(settings_save_one("cfg/large", large, sizeof(large)));
    reinit_fails();
}
ZTEST(readiness_gate, test_unexpected_namespace_blocks_ready) {
    uint8_t bytes[4] = {1};
    zassert_ok(settings_save_one("alien/value", bytes, sizeof(bytes)));
    reinit_fails();
}
ZTEST(readiness_gate, test_orphan_is_not_missing) {
    zassert_ok(crux_config_save(&speed));
    void *storage = NULL;
    zassert_ok(settings_storage_get(&storage));
    zassert_ok(nvs_delete(storage, 0xc001));
    reinit_fails();
}
ZTEST(readiness_gate, test_missing_count_is_not_fresh_default) {
    zassert_ok(nvs_delete(&provision, 0x8000));
    reinit_fails();
}
ZTEST(readiness_gate, test_hard_startup_read_fault_requires_explicit_reinit) {
    gate_reset();
    settings_subsys_initialized = false;
    gate_fail_read = 1;
    zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_ERR_IO);
    gate_fail_read = 0;
    faulted();
    zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_ERR_NOT_READY);
    /* Same explicitly provisioned clean media, no erase or automatic repair. */
    gate_reset();
    settings_subsys_initialized = false;
    zassert_ok(crux_config_zephyr_init());
    no_dispatch();
    zassert_ok(crux_config_save(&speed));
}
ZTEST(readiness_gate, test_crc_valid_malformed_wire_uses_configuration_policy) {
    uint8_t bad[8] = {99, 2, 7, 0, 99, 0, 0, 0};
    zassert_ok(settings_save_one("cfg/speed", bad, sizeof(bad)));
    gate_reset();
    settings_subsys_initialized = false;
    zassert_ok(crux_config_zephyr_init());
    no_dispatch();
    zassert_equal(crux_config_load(&speed), CRUX_CONFIG_DEFAULT_REJECTED);
    crux_config_value_t v;
    zassert_ok(crux_config_read(&speed, &v));
    zassert_equal(v.data.u32, 11);
}
ZTEST(readiness_gate, test_malformed_count_blocks_ready) {
    uint8_t bad = 1;
    zassert_equal(nvs_write(&provision, 0x8000, &bad, 1), 1);
    reinit_fails();
}
ZTEST(readiness_gate, test_masked_backend_init_denial_cannot_enable_writes) {
    zassert_ok(flash_erase(real, PARTITION_OFFSET(candidate_nvs), PARTITION_SIZE(candidate_nvs)));
    mask_init_error = true;
    reinit_fails();
    zassert_true(gate_denied > 0);
}
ZTEST(readiness_gate, test_masked_write_error_cannot_acknowledge_save) {
    gate_write_error = -EIO;
    mask_save_error = true;
    zassert_equal(crux_config_save(&speed), CRUX_CONFIG_ERR_IO);
    gate_write_error = 0;
    mask_save_error = false;
    faulted();
}
ZTEST(readiness_gate, test_masked_load_read_error_cannot_publish_default) {
    zassert_ok(crux_config_save(&speed));
    counts();
    gate_fail_read = 1;
    mask_load_error = true;
    zassert_equal(crux_config_load(&speed), CRUX_CONFIG_ERR_IO);
    gate_fail_read = 0;
    mask_load_error = false;
    faulted();
}
ZTEST(readiness_gate, test_no_space_mapping_is_preserved_when_gate_closes) {
    gate_write_error = -ENOSPC;
    zassert_equal(crux_config_save(&speed), CRUX_CONFIG_ERR_NO_SPACE);
    gate_write_error = 0;
    faulted();
}
ZTEST_SUITE(readiness_gate, NULL, NULL, before, NULL, NULL);
