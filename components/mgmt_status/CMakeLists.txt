set(CRUX_MGMT_STATUS_REFRESH_MS 1000 CACHE STRING "The most often status reads its System and clock sources")

add_library(crux_mgmt_status_api INTERFACE)
target_include_directories(crux_mgmt_status_api INTERFACE include)
target_link_libraries(crux_mgmt_status_api INTERFACE crux::base crux_access_api crux_system_api)

# The core calls the private source ports in src/mgmt_status_port.h. No provider is linked here, so a
# product image links only once a composition selects one for each. It needs only the character rule
# from Configuration, which is a library of its own, so it needs no Configuration provider.
add_library(crux_mgmt_status_core STATIC src/mgmt_status.c src/mgmt_status_json.c)
target_include_directories(crux_mgmt_status_core PRIVATE src)
target_link_libraries(crux_mgmt_status_core PUBLIC crux_mgmt_status_api
    PRIVATE crux::sync crux_configuration_text)
target_compile_definitions(crux_mgmt_status_core PRIVATE
    CRUX_MGMT_STATUS_REFRESH_MS=${CRUX_MGMT_STATUS_REFRESH_MS}u)
target_compile_options(crux_mgmt_status_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_mgmt_status INTERFACE)
add_library(crux::mgmt_status ALIAS crux_mgmt_status)
target_link_libraries(crux_mgmt_status INTERFACE crux_mgmt_status_core crux::access)

# Thin providers, which a composition links by name. They are objects, so that a final link always
# takes them, and a second provider of a port is a duplicate symbol.
if(TARGET crux::time AND TARGET crux::netif AND TARGET crux::system)
    add_library(crux_mgmt_status_sources OBJECT src/source_system.c src/source_time.c
        src/source_netif.c)
    target_include_directories(crux_mgmt_status_sources PRIVATE src)
    target_link_libraries(crux_mgmt_status_sources PUBLIC crux_mgmt_status_api crux::system
        crux::time crux::netif)
    target_compile_options(crux_mgmt_status_sources PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(crux_mgmt_status_no_security OBJECT src/source_no_security.c)
target_include_directories(crux_mgmt_status_no_security PRIVATE src)
target_link_libraries(crux_mgmt_status_no_security PUBLIC crux_mgmt_status_api)
target_compile_options(crux_mgmt_status_no_security PRIVATE -Wall -Wextra -Wpedantic -Werror)
