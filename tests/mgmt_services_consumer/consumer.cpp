/* The same public headers from C++17. It is compiled and not run. */
#include <crux/config_service.h>
#include <crux/management_status.h>

extern "C" int consumer_cpp_status(crux_access_session_t session)
{
    size_t cursor = 0;
    crux_mgmt_status_entry_t entry{};
    return crux_mgmt_status_next(session, &cursor, &entry) == CRUX_OK ? 0 : 1;
}

extern "C" int consumer_cpp_config(crux_access_session_t session)
{
    size_t cursor = 0;
    crux_config_service_item_t item{};
    return crux_config_service_next(session, &cursor, &item) == CRUX_CONFIG_SERVICE_OK ? 0 : 1;
}

/* A string default is set at run time, because a union's first member takes a brace initialiser and
 * designators are C++20. */
extern "C" int consumer_cpp_string_parameter(crux_config_param_t *parameter, char *storage,
                                             size_t capacity)
{
    static crux_config_spec_t spec{"cpp_text", 1, {CRUX_CONFIG_STRING, {false}}, nullptr, nullptr};
    spec.default_value.data.string.text = "crux";
    spec.default_value.data.string.length = 4;
    return crux_config_init_string(parameter, &spec, storage, capacity) == CRUX_CONFIG_OK ? 0 : 1;
}
