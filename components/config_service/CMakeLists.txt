add_library(crux_config_service_api INTERFACE)
target_include_directories(crux_config_service_api INTERFACE include)
target_link_libraries(crux_config_service_api INTERFACE crux_access_api crux::configuration)

# The core calls the private event port in src/config_service_port.h. No provider is linked here, so
# a product image links only once a composition supplies one, which Issue #182 will. The access and
# Configuration providers also come from the composition, so that tests can use fakes.
add_library(crux_config_service_core STATIC src/config_service.c)
target_include_directories(crux_config_service_core PRIVATE src)
target_link_libraries(crux_config_service_core PUBLIC crux_config_service_api PRIVATE crux::sync)
target_compile_options(crux_config_service_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_config_service INTERFACE)
add_library(crux::config_service ALIAS crux_config_service)
target_link_libraries(crux_config_service INTERFACE crux_config_service_core crux::access)
