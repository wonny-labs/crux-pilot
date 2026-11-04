#include <crux/configuration.h>

#include <cstring>

/* A C++17 consumer sets a string default at run time: bool stays the union's first member, so it
 * alone takes a brace initialiser, and designators are C++20. */
extern "C" int configuration_string_consumer(void)
{
    crux_config_spec_t spec{"cpp_text", 3, {CRUX_CONFIG_STRING, {false}}, nullptr, nullptr};
    spec.default_value.data.string.text = "crux";
    spec.default_value.data.string.length = 4;
    static char storage[CRUX_CONFIG_STRING_MAX + 1];
    crux_config_param_t parameter{};
    crux_config_value_t out{CRUX_CONFIG_BOOL, {false}};
    if (crux_config_init_string(&parameter, &spec, storage, sizeof(storage)) != CRUX_CONFIG_OK ||
        crux_config_load(&parameter) != CRUX_CONFIG_DEFAULT_MISSING ||
        crux_config_read(&parameter, &out) != CRUX_CONFIG_OK ||
        std::strcmp(out.data.string.text, "crux") != 0 ||
        crux_config_save(&parameter) != CRUX_CONFIG_OK) {
        return 1;
    }
    crux_config_value_t candidate{CRUX_CONFIG_STRING, {false}};
    candidate.data.string.text = "other";
    candidate.data.string.length = 5;
    if (crux_config_update(&parameter, &candidate) != CRUX_CONFIG_OK ||
        crux_config_load(&parameter) != CRUX_CONFIG_OK ||
        crux_config_read(&parameter, &out) != CRUX_CONFIG_OK ||
        std::strcmp(out.data.string.text, "crux") != 0) {
        return 2;
    }
    return 0;
}
