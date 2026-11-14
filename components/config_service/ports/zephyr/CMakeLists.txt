# The Configuration service on Zephyr (ADR-0004 phase 0, Experimental): the portable core and the provider of
# its event port over the security events. The module's CMake adds this directory only under
# CONFIG_CRUX_CONFIG_SERVICE, which needs CONFIG_CRUX_ACCESS and selects the Configuration core.
#
# No crux:: target and no public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are
# the ones the Host build lists in components/config_service/CMakeLists.txt, and
# tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_CONFIG_SERVICE_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_CONFIG_SERVICE_COMPONENT_DIR}/.." ABSOLUTE)

if(NOT CONFIG_CRUX_CONFIG_SERVICE)
  message(FATAL_ERROR "This directory is the Configuration service selection of the module: set CONFIG_CRUX_CONFIG_SERVICE")
endif()

add_library(crux_config_service_zephyr STATIC
  ../../src/config_service.c
  ../../src/config_service_events.c
)
target_include_directories(crux_config_service_zephyr PRIVATE
  "${CRUX_CONFIG_SERVICE_COMPONENT_DIR}/include"
  "${CRUX_CONFIG_SERVICE_COMPONENT_DIR}/src"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/configuration/include"
  "${CRUX_COMPONENTS_DIR}/security_event/include")
zephyr_append_cmake_library(crux_config_service_zephyr)
target_link_libraries(crux_config_service_zephyr PRIVATE zephyr_interface crux::base crux::sync)
add_dependencies(crux_config_service_zephyr zephyr_generated_headers)
target_compile_options(crux_config_service_zephyr PRIVATE -Wall -Wextra -Werror)
