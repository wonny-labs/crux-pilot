/* SPDX-License-Identifier: Apache-2.0 */
/* One fixed trial through existing public APIs, shared with the real-engine fixture. */
#include <crux/configuration.h>
#include <crux/file.h>
#include <errno.h>
#include <string.h>
static bool valid_cycles(const crux_config_value_t *v, void *unused) {
    (void)unused;
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
int crux_candidate_values_readback(void);
int crux_candidate_values_trial(void) {
    crux_config_param_t cycles = {0}, diagnostics = {0};
    crux_file_t *file = NULL;
    size_t actual = 0;
    if (crux_config_init(&cycles, &cycles_spec) ||
        crux_config_init(&diagnostics, &diagnostics_spec))
        return -EIO;
    /* Exactly two keys, two successive scalar values each: four explicit saves. */
    for (unsigned i = 0; i < 2; ++i) {
        crux_config_value_t c = {.type = CRUX_CONFIG_U32, .data.u32 = i ? 7 : 3};
        crux_config_value_t d = {.type = CRUX_CONFIG_BOOL, .data.boolean = i != 0};
        if (crux_config_update(&cycles, &c) || crux_config_update(&diagnostics, &d) ||
            crux_config_save(&cycles) || crux_config_save(&diagnostics))
            return -EIO;
    }
    if (crux_file_open("candidate", CRUX_FILE_CREATE_REPLACE, &file))
        return -EIO;
    bool failed = crux_file_write(file, "maintenance", 11, &actual) != CRUX_FILE_OK || actual != 11;
    if (!failed)
        failed = crux_file_sync(file) != CRUX_FILE_OK;
    /* Always consume close once, including an uncertain sync/write. */
    crux_file_result_t closed = crux_file_close(&file);
    if (failed || closed != CRUX_FILE_OK)
        return -EIO;
    return crux_candidate_values_readback();
}
int crux_candidate_values_readback(void) {
    crux_config_param_t cycles = {0}, diagnostics = {0};
    crux_file_t *file = NULL;
    size_t actual = 0;
    char bytes[16] = {0};
    if (crux_config_init(&cycles, &cycles_spec) ||
        crux_config_init(&diagnostics, &diagnostics_spec))
        return -EIO;
    if (crux_file_open("candidate", CRUX_FILE_READ, &file))
        return -EIO;
    bool failed = crux_file_read(file, bytes, sizeof(bytes), &actual) != CRUX_FILE_OK ||
                  actual != 11 || memcmp(bytes, "maintenance", 11) != 0;
    crux_file_result_t closed = crux_file_close(&file);
    if (failed || closed != CRUX_FILE_OK)
        return -EIO;
    if (crux_config_load(&cycles) || crux_config_load(&diagnostics))
        return -EIO;
    crux_config_value_t c, d;
    if (crux_config_read(&cycles, &c) || crux_config_read(&diagnostics, &d) ||
        c.type != CRUX_CONFIG_U32 || c.data.u32 != 7 || d.type != CRUX_CONFIG_BOOL ||
        !d.data.boolean)
        return -EIO;
    return 0;
}
