add_library(crux_config_service_api INTERFACE)
target_include_directories(crux_config_service_api INTERFACE include)
target_link_libraries(crux_config_service_api INTERFACE crux_access_api crux::configuration)

# The core calls the private event port in src/config_service_port.h. No provider is linked here, so
# a product image links only once a composition supplies one, as crux_config_service_security_events
# below. The access and Configuration providers also come from the composition, so that tests can use
# fakes.
add_library(crux_config_service_core STATIC src/config_service.c)
target_include_directories(crux_config_service_core PRIVATE src)
target_link_libraries(crux_config_service_core PUBLIC crux_config_service_api PRIVATE crux::sync)
target_compile_options(crux_config_service_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_config_service INTERFACE)
add_library(crux::config_service ALIAS crux_config_service)
target_link_libraries(crux_config_service INTERFACE crux_config_service_core crux::access)

# The provider of the event port, which a composition links by name. It is an object target, so that
# a final link always takes it, and a second provider of the port is a duplicate symbol.
if(TARGET crux_security_event_api)
    add_library(crux_config_service_security_events OBJECT src/config_service_events.c)
    target_include_directories(crux_config_service_security_events PRIVATE src)
    target_link_libraries(crux_config_service_security_events
        PUBLIC crux_config_service_api crux_security_event_api)
    target_compile_options(crux_config_service_security_events PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
