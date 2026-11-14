# The Configuration core on Zephyr (ADR-0004 phase 0, Experimental): the parameter mechanism, the character
# rule, and the port for a build that has no provider yet (#16), which answers NOT_SUPPORTED to a load and a
# save instead of the image failing to link. The module's CMake adds this directory only under
# CONFIG_CRUX_CONFIGURATION_CORE, which CONFIG_CRUX_CONFIG_SERVICE and CONFIG_CRUX_MGMT_STATUS select.
#
# The Settings File selection (CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE, ports/zephyr/CMakeLists.txt) compiles
# configuration.c and configuration_text.c itself, beside its own port. With that selection on, this directory
# builds nothing, so that no symbol is defined twice and no no-provider port competes with the real one.
#
# No crux:: target and no public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are
# the ones the Host build lists in components/configuration/CMakeLists.txt, and
# tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_CONFIGURATION_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT CONFIG_CRUX_CONFIGURATION_CORE)
  message(FATAL_ERROR "This directory is the Configuration core selection of the module: set CONFIG_CRUX_CONFIGURATION_CORE")
endif()
if(CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE)
  return()
endif()

add_library(crux_configuration_core_zephyr STATIC
  ../../../src/configuration.c
  ../../../src/configuration_text.c
  ../../../src/configuration_no_provider.c
)
target_include_directories(crux_configuration_core_zephyr PRIVATE
  "${CRUX_CONFIGURATION_COMPONENT_DIR}/include"
  "${CRUX_CONFIGURATION_COMPONENT_DIR}/src")
zephyr_append_cmake_library(crux_configuration_core_zephyr)
target_link_libraries(crux_configuration_core_zephyr PRIVATE zephyr_interface crux::base)
add_dependencies(crux_configuration_core_zephyr zephyr_generated_headers)
target_compile_options(crux_configuration_core_zephyr PRIVATE -Wall -Wextra -Werror)
