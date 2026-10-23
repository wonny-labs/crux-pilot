#include "fault_provider.h"
#include <crux/configuration.h>
#include <stdio.h>
#include <string.h>

static unsigned checks, failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        ++checks;                                                                                  \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static crux_config_value_t u32(uint32_t value) {
    crux_config_value_t result = {.type = CRUX_CONFIG_U32, .data.u32 = value};
    return result;
}
static bool range(const crux_config_value_t *value, void *context) {
    uint32_t maximum = *(uint32_t *)context;
    return value->type == CRUX_CONFIG_U32 && value->data.u32 >= 1 && value->data.u32 <= maximum;
}
static uint32_t current(const crux_config_param_t *parameter) {
    crux_config_value_t value = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_read(parameter, &value) == CRUX_CONFIG_OK);
    return value.data.u32;
}
static void scalar_cases(void) {
    config_fault_reset();
    uint32_t maximum = 100;
    crux_config_spec_t spec = {"report_every_cycles", 0x1234, u32(5), range, &maximum};
    crux_config_param_t parameter = {0};
    crux_config_value_t out = {.type = CRUX_CONFIG_BOOL}, candidate = u32(1);
    CHECK(crux_config_init(NULL, &spec) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_init(&parameter, NULL) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_read(&parameter, &out) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);
    CHECK(current(&parameter) == 5);
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_ERR_INVALID_STATE);
    CHECK(current(&parameter) == 5);
    CHECK(crux_config_read(NULL, &out) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_read(&parameter, NULL) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_update(NULL, &candidate) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_update(&parameter, NULL) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_load(NULL) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_save(NULL) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(current(&parameter) == 1);
    candidate = u32(100);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    candidate = u32(101);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current(&parameter) == 100);
    candidate = u32(0);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    candidate.type = CRUX_CONFIG_BOOL;
    candidate.data.boolean = true;
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(config_fault_reads() == 0 && config_fault_writes() == 0);

    const char *bad[] = {NULL, "", ".", "..", "root/name", "bad key", "\xc3\xa9"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        crux_config_spec_t invalid = spec;
        crux_config_param_t fresh = {0};
        invalid.key = bad[i];
        CHECK(crux_config_init(&fresh, &invalid) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
        CHECK(fresh.spec == NULL);
    }
    crux_config_param_t fresh = {0};
    crux_config_spec_t invalid = spec;
    invalid.key = "12345678901234567890123456789012";
    CHECK(crux_config_init(&fresh, &invalid) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    invalid.key = "1234567890123456789012345678901";
    CHECK(crux_config_init(&fresh, &invalid) == CRUX_CONFIG_OK);
    fresh = (crux_config_param_t){0};
    invalid = spec;
    invalid.default_value.type = (crux_config_type_t)99;
    CHECK(crux_config_init(&fresh, &invalid) == CRUX_CONFIG_ERR_INVALID_ARGUMENT);
    CHECK(fresh.spec == NULL);
    invalid = spec;
    invalid.default_value = u32(101);
    CHECK(crux_config_init(&fresh, &invalid) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(fresh.spec == NULL);
}

static void persistence_cases(void) {
    config_fault_reset();
    uint32_t maximum = 100;
    crux_config_spec_t spec = {"report_every_cycles", 0x1234, u32(5), range, &maximum};
    crux_config_param_t parameter = {0};
    crux_config_value_t candidate = u32(42);
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_MISSING);
    CHECK(current(&parameter) == 5 && config_fault_writes() == 0);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_OK);
    const uint8_t expected[] = {1, 2, 0x34, 0x12, 42, 0, 0, 0};
    uint8_t bytes[16] = {0};
    CHECK(config_fault_bytes(spec.key, bytes, sizeof(bytes)) == sizeof(expected));
    CHECK(memcmp(bytes, expected, sizeof(expected)) == 0);
    candidate = u32(77);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_OK);
    CHECK(current(&parameter) == 42);
    const uint8_t maximum_u32[] = {1, 2, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff};
    crux_config_spec_t unconstrained = {"wide", 0x1234, u32(0), NULL, NULL};
    crux_config_param_t wide = {0};
    CHECK(crux_config_init(&wide, &unconstrained) == CRUX_CONFIG_OK);
    config_fault_record("wide", maximum_u32, sizeof(maximum_u32));
    CHECK(crux_config_load(&wide) == CRUX_CONFIG_OK && current(&wide) == UINT32_MAX);
    CHECK(crux_config_save(&wide) == CRUX_CONFIG_OK);
    CHECK(config_fault_bytes("wide", bytes, sizeof(bytes)) == sizeof(maximum_u32));
    CHECK(memcmp(bytes, maximum_u32, sizeof(maximum_u32)) == 0);

    for (unsigned field = 0; field < 5; ++field) {
        uint8_t bad[8];
        memcpy(bad, expected, sizeof(bad));
        bad[field] = field == 4 ? 101 : 99;
        config_fault_record(spec.key, bad, sizeof(bad));
        CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
        CHECK(current(&parameter) == 5);
    }
    config_fault_record(spec.key, expected, sizeof(expected));
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_OK, 2});
    candidate = u32(77);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_IO);
    CHECK(current(&parameter) == 77);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_ERR_IO, SIZE_MAX});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_IO);
    CHECK(current(&parameter) == 77);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_ERR_NOT_READY, SIZE_MAX});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_NOT_READY);
    config_fault_read((config_fault_read_options_t){(crux_config_result_t)99, SIZE_MAX});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_IO);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_ERR_NOT_SUPPORTED, SIZE_MAX});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_NOT_SUPPORTED);
    CHECK(current(&parameter) == 77);
    config_fault_read((config_fault_read_options_t){(crux_config_result_t)-99, SIZE_MAX});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_IO);
    CHECK(current(&parameter) == 77);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_OK, SIZE_MAX});

    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_NO_SPACE, 0});
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_ERR_NO_SPACE);
    CHECK(current(&parameter) == 77);
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_OK && current(&parameter) == 42);
    candidate = u32(66);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_IO, 1});
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_ERR_IO);
    CHECK(current(&parameter) == 66);
    crux_config_param_t restarted = {0};
    CHECK(crux_config_init(&restarted, &spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&restarted) == CRUX_CONFIG_OK && current(&restarted) == 66);
    config_fault_write((config_fault_write_options_t){(crux_config_result_t)88, 0});
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_ERR_IO);
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_OK, 0});
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_OK);

    maximum = 1;
    size_t writes = config_fault_writes();
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(config_fault_writes() == writes && current(&parameter) == 66);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_DEFAULT_MISSING, 0});
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current(&parameter) == 66);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_OK, SIZE_MAX});
    const uint8_t malformed[] = {99, 2, 0x34, 0x12, 42, 0, 0, 0};
    config_fault_record(spec.key, malformed, sizeof(malformed));
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_ERR_REJECTED);
    CHECK(current(&parameter) == 66);
    config_fault_read((config_fault_read_options_t){CRUX_CONFIG_DEFAULT_MISSING, 0});
    maximum = 100;
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_MISSING);
    CHECK(current(&parameter) == 5);
}

static void bool_and_length_cases(void) {
    config_fault_reset();
    crux_config_spec_t spec = {
        "diagnostics_enabled", 7, {.type = CRUX_CONFIG_BOOL, .data.boolean = false}, NULL, NULL};
    crux_config_param_t parameter = {0};
    crux_config_value_t candidate = {.type = CRUX_CONFIG_BOOL, .data.boolean = true},
                        out = {.type = CRUX_CONFIG_BOOL};
    const uint8_t valid[] = {1, 1, 7, 0, 1};
    CHECK(crux_config_init(&parameter, &spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_update(&parameter, &candidate) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&parameter) == CRUX_CONFIG_OK);
    uint8_t bytes[16] = {0};
    CHECK(config_fault_bytes(spec.key, bytes, sizeof(bytes)) == sizeof(valid));
    CHECK(memcmp(bytes, valid, sizeof(valid)) == 0);
    for (size_t length = 0; length <= sizeof(bytes); ++length) {
        memcpy(bytes, valid, sizeof(valid));
        config_fault_record(spec.key, bytes, length);
        crux_config_result_t result = crux_config_load(&parameter);
        CHECK(result == (length == sizeof(valid) ? CRUX_CONFIG_OK : CRUX_CONFIG_DEFAULT_REJECTED));
    }
    const uint8_t invalid[] = {1, 1, 7, 0, 2};
    config_fault_record(spec.key, invalid, sizeof(invalid));
    CHECK(crux_config_load(&parameter) == CRUX_CONFIG_DEFAULT_REJECTED);
    CHECK(crux_config_read(&parameter, &out) == CRUX_CONFIG_OK && !out.data.boolean);
}

/* Recovery can observe independently valid records from different save attempts. */
static void mixed_key_cases(void) {
    config_fault_reset();
    crux_config_spec_t report_spec = {"report", 1, u32(5), NULL, NULL};
    crux_config_spec_t diagnostics_spec = {
        "diagnostics", 1, {.type = CRUX_CONFIG_BOOL, .data.boolean = false}, NULL, NULL};
    crux_config_param_t report = {0}, diagnostics = {0};
    CHECK(crux_config_init(&report, &report_spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&diagnostics, &diagnostics_spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&report) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&diagnostics) == CRUX_CONFIG_OK);
    crux_config_value_t new_report = u32(7);
    crux_config_value_t new_diagnostics = {.type = CRUX_CONFIG_BOOL, .data.boolean = true};
    CHECK(crux_config_update(&report, &new_report) == CRUX_CONFIG_OK);
    CHECK(crux_config_update(&diagnostics, &new_diagnostics) == CRUX_CONFIG_OK);
    CHECK(crux_config_save(&report) == CRUX_CONFIG_OK);
    config_fault_write((config_fault_write_options_t){CRUX_CONFIG_ERR_IO, 0});
    CHECK(crux_config_save(&diagnostics) == CRUX_CONFIG_ERR_IO);
    crux_config_value_t accepted = {.type = CRUX_CONFIG_BOOL};
    CHECK(crux_config_read(&diagnostics, &accepted) == CRUX_CONFIG_OK && accepted.data.boolean);
    crux_config_param_t restarted_report = {0}, restarted_diagnostics = {0};
    CHECK(crux_config_init(&restarted_report, &report_spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_init(&restarted_diagnostics, &diagnostics_spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&restarted_report) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&restarted_diagnostics) == CRUX_CONFIG_OK);
    CHECK(current(&restarted_report) == 7);
    CHECK(crux_config_read(&restarted_diagnostics, &accepted) == CRUX_CONFIG_OK &&
          !accepted.data.boolean);
    /* No batch consistency is inferred; application decides whether to activate this pair. */
}

int crux_configuration_conformance(void) {
    checks = failures = 0;
    scalar_cases();
    persistence_cases();
    bool_and_length_cases();
    mixed_key_cases();
    printf("CONFIGURATION_%s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks,
           failures);
    return failures ? 1 : 0;
}
