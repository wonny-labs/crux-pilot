#include <crux/configuration.h>

int configuration_consumer(void) {
    const crux_config_spec_t spec = {
        "second_schema", 9, {.type = CRUX_CONFIG_BOOL, .data.boolean = true}, 0, 0};
    crux_config_param_t parameter = {0};
    crux_config_value_t out = {.type = CRUX_CONFIG_BOOL};
    if (crux_config_init(&parameter, &spec) != CRUX_CONFIG_OK ||
        crux_config_load(&parameter) != CRUX_CONFIG_DEFAULT_MISSING ||
        crux_config_read(&parameter, &out) != CRUX_CONFIG_OK || !out.data.boolean ||
        crux_config_save(&parameter) != CRUX_CONFIG_OK) {
        return 1;
    }
    out.data.boolean = false;
    if (crux_config_update(&parameter, &out) != CRUX_CONFIG_OK ||
        crux_config_load(&parameter) != CRUX_CONFIG_OK ||
        crux_config_read(&parameter, &out) != CRUX_CONFIG_OK || !out.data.boolean) {
        return 2;
    }
    return 0;
}
