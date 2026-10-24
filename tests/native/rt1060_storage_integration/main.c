/* SPDX-License-Identifier: Apache-2.0 */
/* Actual engine, not a NOR emulator: controlled command leaf delegates to
 * Zephyr's existing Flash simulator. Actual RAM dispatcher is tested separately. */
#include "cmdline.h"
#include "configuration_settings.h"
#include "gate.h"
#include "maintenance.h"
#include "storage_adapter.h"
#include "storage_native.h"
#include <crux/configuration.h>
#include <crux/file.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>
static uint8_t read_bytes[256];
static size_t read_length;
static unsigned commands, file_mutations, nvs_mutations, scratch_commands, scratch_mutations;
static int prereq;
static bool scratch_fail;
int crux_candidate_command_prerequisites(void) {
    return prereq;
}
int crux_rom_command_submit(const struct crux_rom_request *r) {
    const struct device *sim = PARTITION_DEVICE(candidate_file);
    zassert_true(crux_pilot_flash_owner_request_valid(r->owner, r->offset, r->length,
                                                      r->operation == CRUX_ROM_SECTOR));
    ++commands;
    if (r->owner == CRUX_PILOT_FLASH_SCRATCH)
        ++scratch_commands;
    if (r->operation != CRUX_ROM_READ) {
        if (r->owner == CRUX_PILOT_FLASH_SCRATCH)
            ++scratch_mutations;
        if (r->owner == CRUX_PILOT_FLASH_FILE)
            ++file_mutations;
        if (r->owner == CRUX_PILOT_FLASH_NVS)
            ++nvs_mutations;
    }
    if (scratch_fail && r->owner == CRUX_PILOT_FLASH_SCRATCH &&
        r->operation == CRUX_ROM_PROGRAM)
        return -EIO;
    if (r->operation == CRUX_ROM_READ) {
        read_length = r->length;
        return flash_read(sim, r->offset, read_bytes, r->length);
    }
    if (r->operation == CRUX_ROM_PROGRAM)
        return flash_write(sim, r->offset, r->payload, r->length);
    return flash_erase(sim, r->offset, r->length);
}
int crux_rom_command_read_result(void *bytes, size_t length) {
    zassert_equal(length, read_length);
    memcpy(bytes, read_bytes, length);
    read_length = 0;
    return 0;
}
ZTEST(rt1060_storage_integration, test_fixed_trial_with_deferred_owners_and_one_way_finish) {
    const struct flash_area *area = NULL;
    zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(candidate_file_backend))));
    zassert_equal(crux_candidate_area_open(PARTITION_ID(candidate_file), &area), -EACCES);
    uint8_t bytes[4];
    zassert_equal(crux_candidate_read(CRUX_PILOT_FLASH_FILE, 0x620000, bytes, 4), -EACCES);
    zassert_equal(commands, 0);
    int argc;
    char **argv;
    native_get_test_cmd_line_args(&argc, &argv);
    bool refused = argc > 0 && strcmp(argv[0], "refused") == 0;
    bool scratch = argc > 0 && strcmp(argv[0], "scratch") == 0;
    scratch_fail = argc > 0 && strcmp(argv[0], "scratch-fail") == 0;
    bool unprovisioned = argc > 0 && strcmp(argv[0], "unprovisioned") == 0;
    if (refused) {
        prereq = -EACCES;
        zassert_equal(crux_candidate_fixed_trial(true), -EACCES);
        zassert_equal(crux_candidate_fixed_trial(true), -EACCES);
        zassert_equal(crux_candidate_scratch_trial(), -EACCES);
        zassert_equal(commands, 0);
        zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(candidate_file_backend))));
        printk("REFUSED_PREREQUISITES commands=0 devices_ready=0\n");
        return;
    }
    const struct device *sim = PARTITION_DEVICE(candidate_scratch);
    static uint8_t seed[4096], observed[4096];
    memset(seed, 0xa5, sizeof(seed));
    zassert_ok(flash_write(sim, PARTITION_OFFSET(candidate_scratch), seed, sizeof(seed)));
    if (scratch || scratch_fail) {
        zassert_equal(crux_candidate_scratch_trial(), scratch_fail ? -EIO : 0);
        zassert_equal(commands, scratch_fail ? 2 : 3);
        zassert_equal(scratch_commands, commands);
        zassert_equal(scratch_mutations, 2);
        zassert_equal(file_mutations, 0);
        zassert_equal(nvs_mutations, 0);
        zassert_equal(gate_reads + gate_writes + gate_erases, 0);
        zassert_equal(maintenance_file_dispatches, 0);
        zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(candidate_file_backend))));
        zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(candidate_nvs_backend))));
        zassert_false(crux_candidate_storage_ready());
        zassert_ok(flash_read(sim, PARTITION_OFFSET(candidate_scratch), observed, sizeof(observed)));
        memset(seed, 0xff, sizeof(seed));
        if (!scratch_fail) {
            uint8_t expected[] = {0x31, 0x42, 0x53, 0x64};
            memcpy(seed, expected, sizeof(expected));
        }
        zassert_mem_equal(seed, observed, sizeof(seed));
        unsigned before = commands;
        zassert_equal(crux_candidate_scratch_trial(), -EACCES);
        zassert_equal(crux_candidate_fixed_trial(true), -EACCES);
        crux_file_t *file = NULL;
        zassert_equal(crux_file_open("candidate", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
        zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_ERR_NOT_READY);
        static const crux_config_spec_t scratch_spec = {
            .key = "report_every_cycles", .schema_version = 1,
            .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 1}};
        crux_config_param_t config = {0};
        zassert_ok(crux_config_init(&config, &scratch_spec));
        zassert_equal(crux_config_load(&config), CRUX_CONFIG_ERR_NOT_READY);
        zassert_equal(crux_config_save(&config), CRUX_CONFIG_ERR_NOT_READY);
        zassert_equal(commands, before);
        printk("SCRATCH_ONLY failed=%u commands=%u mutations=%u one_way=1\n",
               scratch_fail, commands, scratch_mutations);
        return;
    }
    if (unprovisioned) {
        zassert_not_equal(crux_candidate_fixed_trial(false), 0);
        zassert_equal(scratch_commands, 0);
        zassert_equal(scratch_mutations, 0);
        zassert_ok(flash_read(sim, PARTITION_OFFSET(candidate_scratch), observed, sizeof(observed)));
        zassert_mem_equal(seed, observed, sizeof(seed));
        zassert_equal(file_mutations, 0);
        zassert_equal(nvs_mutations, 0);
        zassert_false(crux_candidate_storage_ready());
        unsigned before = commands;
        zassert_equal(crux_candidate_fixed_trial(true), -EACCES);
        zassert_equal(crux_candidate_scratch_trial(), -EACCES);
        zassert_equal(commands, before);
        printk("UNPROVISIONED refused=1 file_mutations=0 nvs_mutations=0 one_way=1\n");
        return;
    }
    zassert_ok(crux_candidate_fixed_trial(true));
    zassert_false(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(candidate_scratch_backend))));
    zassert_equal(scratch_commands, 0);
    zassert_equal(scratch_mutations, 0);
    zassert_ok(flash_read(sim, PARTITION_OFFSET(candidate_scratch), observed, sizeof(observed)));
    zassert_mem_equal(seed, observed, sizeof(seed));
    zassert_true(commands > 0);
    zassert_false(crux_candidate_storage_ready());
    unsigned before = commands;
    zassert_equal(crux_candidate_fixed_trial(true), -EACCES);
    zassert_equal(crux_candidate_scratch_trial(), -EACCES);
    zassert_equal(crux_candidate_read(CRUX_PILOT_FLASH_FILE, 0x620000, bytes, 4), -EACCES);
    zassert_equal(crux_candidate_area_open(PARTITION_ID(candidate_file), &area), -EACCES);
    crux_file_t *file = NULL;
    zassert_equal(crux_file_open("candidate", CRUX_FILE_READ, &file), CRUX_FILE_ERR_NOT_READY);
    zassert_equal(crux_file_remove("candidate"), CRUX_FILE_ERR_NOT_READY);
    static const crux_config_spec_t spec = {
        .key = "report_every_cycles",
        .schema_version = 1,
        .default_value = {.type = CRUX_CONFIG_U32, .data.u32 = 1}};
    crux_config_param_t config = {0};
    zassert_ok(crux_config_init(&config, &spec));
    zassert_equal(crux_config_zephyr_init(), CRUX_CONFIG_ERR_NOT_READY);
    zassert_equal(crux_config_load(&config), CRUX_CONFIG_ERR_NOT_READY);
    zassert_equal(crux_config_save(&config), CRUX_CONFIG_ERR_NOT_READY);
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 29};
    zassert_ok(crux_config_update(&config, &value));
    value.data.u32 = 0;
    zassert_ok(crux_config_read(&config, &value));
    zassert_equal(value.data.u32, 29);
    zassert_equal(commands, before);
    printk("FIXED_TRIAL commands=%u settings_reads=%u writes=%u erases=%u one_way=1\n", commands,
           gate_reads, gate_writes, gate_erases);
}
ZTEST_SUITE(rt1060_storage_integration, NULL, NULL, NULL, NULL, NULL);
