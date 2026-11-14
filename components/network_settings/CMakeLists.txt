add_library(crux_network_settings_api INTERFACE)
target_include_directories(crux_network_settings_api INTERFACE include)
target_link_libraries(crux_network_settings_api INTERFACE crux_access_api crux_config_service_api)

set(CRUX_NETWORK_SETTINGS_SOURCES
    src/network_settings_check.c
    src/network_settings.c
)
list(TRANSFORM CRUX_NETWORK_SETTINGS_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_NETWORK_SETTINGS_SOURCES "${CRUX_NETWORK_SETTINGS_SOURCES}" CACHE INTERNAL
    "Portable network settings sources")

# The core calls the private ports in src/network_settings_port.h: the apply port, which is private
# native code, and the event port. No provider is linked here, so that an image links only once a
# composition chooses one, as for the Configuration service. The fakes in tests/network_settings are
# a test composition that exists only when Crux builds its own tests.
add_library(crux_network_settings_core STATIC ${CRUX_NETWORK_SETTINGS_SOURCES})
target_include_directories(crux_network_settings_core PRIVATE src)
target_link_libraries(crux_network_settings_core
    PUBLIC crux_network_settings_api
    PRIVATE crux::config_service crux::configuration crux::access crux::sync crux::thread)
target_compile_options(crux_network_settings_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_network_settings INTERFACE)
add_library(crux::network_settings ALIAS crux_network_settings)
target_link_libraries(crux_network_settings INTERFACE crux_network_settings_api crux_network_settings_core)

# The apply port for a composition that applies nothing, such as `native_sim` today: it reports that
# it applied the settings, and reports no running values, so that GET shows none. Chosen by name,
# like crux_configuration_no_provider. An object target, so that a final link always takes it.
add_library(crux_network_settings_no_apply OBJECT src/network_settings_no_apply.c)
target_include_directories(crux_network_settings_no_apply PRIVATE src)
target_link_libraries(crux_network_settings_no_apply PUBLIC crux_network_settings_api)
target_compile_options(crux_network_settings_no_apply PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The provider of the event port over the security events, which a composition links by name.
if(TARGET crux_security_event_api)
    add_library(crux_network_settings_security_events OBJECT src/network_settings_events.c)
    target_include_directories(crux_network_settings_security_events PRIVATE src)
    target_link_libraries(crux_network_settings_security_events
        PUBLIC crux_network_settings_api crux_security_event_api)
    target_compile_options(crux_network_settings_security_events PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The serial recovery command of the command line registry. Not a project's target: the composition
# that owns the console puts crux_network_settings_cli_commands() in its tables.
if(TARGET crux_cli_api)
    add_library(crux_network_settings_cli STATIC src/network_settings_cli.c)
    target_include_directories(crux_network_settings_cli PRIVATE src)
    target_link_libraries(crux_network_settings_cli
        PUBLIC crux_network_settings_api crux_cli_api
        PRIVATE crux::network_settings)
    target_compile_options(crux_network_settings_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
