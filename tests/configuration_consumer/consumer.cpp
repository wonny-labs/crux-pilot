#include <crux/configuration.h>

extern "C" int configuration_consumer(void) {
    const crux_config_spec_t spec{"cpp_schema", 3, {CRUX_CONFIG_BOOL, {false}}, nullptr, nullptr};
    crux_config_param_t parameter{};
    crux_config_value_t out{CRUX_CONFIG_BOOL, {false}};
    if (crux_config_init(&parameter, &spec) != CRUX_CONFIG_OK ||
        crux_config_load(&parameter) != CRUX_CONFIG_DEFAULT_MISSING ||
        crux_config_read(&parameter, &out) != CRUX_CONFIG_OK || out.data.boolean ||
        crux_config_save(&parameter) != CRUX_CONFIG_OK) {
        return 1;
    }
    crux_config_value_t candidate{CRUX_CONFIG_BOOL, {true}};
    if (crux_config_update(&parameter, &candidate) != CRUX_CONFIG_OK ||
        crux_config_load(&parameter) != CRUX_CONFIG_OK ||
        crux_config_read(&parameter, &out) != CRUX_CONFIG_OK || out.data.boolean) {
        return 2;
    }
    return 0;
}
