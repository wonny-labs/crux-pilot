# The Settings File selection: Configuration over the Zephyr Settings File backend, on Reliance
# Edge. The module's CMake adds this directory only under CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE.
get_filename_component(CRUX_CONFIGURATION_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE)
  message(FATAL_ERROR "This directory is the Settings File selection of Configuration")
endif()

add_library(crux_configuration_api INTERFACE)
target_include_directories(crux_configuration_api INTERFACE
  "${CRUX_CONFIGURATION_COMPONENT_DIR}/include")

add_library(crux_configuration_zephyr STATIC
  "${CRUX_CONFIGURATION_COMPONENT_DIR}/src/configuration.c" configuration_settings.c)
target_include_directories(crux_configuration_zephyr PRIVATE
  "${CRUX_CONFIGURATION_COMPONENT_DIR}/src" ".")
zephyr_append_cmake_library(crux_configuration_zephyr)
target_link_libraries(crux_configuration_zephyr PRIVATE crux_configuration_api zephyr_interface)
add_dependencies(crux_configuration_zephyr zephyr_generated_headers)
target_compile_options(crux_configuration_zephyr PRIVATE -Wall -Wextra -Werror)

# The private header of the provider, for a composition that initialises it.
target_include_directories(crux_configuration_api INTERFACE "${CMAKE_CURRENT_LIST_DIR}")

add_library(crux_configuration INTERFACE)
add_library(crux::configuration ALIAS crux_configuration)
target_link_libraries(crux_configuration INTERFACE crux_configuration_api)
