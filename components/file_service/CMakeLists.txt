add_library(crux_file_service_api INTERFACE)
target_include_directories(crux_file_service_api INTERFACE include)
target_link_libraries(crux_file_service_api INTERFACE crux_access_api crux_file_api)

# The core calls the private event port in src/file_service_port.h. No provider is linked here, so a
# product image links only once a composition supplies one, which Issue #182 will. The access
# provider and the File provider come from crux::access and crux::file, so that tests can use fakes.
add_library(crux_file_service_core STATIC src/file_service.c)
target_include_directories(crux_file_service_core PRIVATE src)
target_link_libraries(crux_file_service_core PUBLIC crux_file_service_api PRIVATE crux::sync)
target_compile_options(crux_file_service_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_file_service INTERFACE)
add_library(crux::file_service ALIAS crux_file_service)
target_link_libraries(crux_file_service INTERFACE crux_file_service_core crux::access crux::file)

# The provider of crux_file_service_port_event over the security events (Issue #242), which a
# composition links by name. An object target, so that a final link always takes it, and a second
# provider of the port is a duplicate symbol. The record's subject is the area, never a file name.
if(CRUX_BUILD_SECURITY_EVENT)
    add_library(crux_file_service_security_events OBJECT src/file_service_security_events.c)
    target_include_directories(crux_file_service_security_events PRIVATE src)
    target_link_libraries(crux_file_service_security_events
        PRIVATE crux_file_service_api crux_access_api crux_security_event_api)
    target_compile_options(crux_file_service_security_events PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
