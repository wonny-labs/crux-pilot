# The status service on Zephyr (ADR-0004 phase 0, Experimental): the portable core and the sources of its
# items over the System, Time, network interface, credential store and security event contracts. The
# module's CMake adds this directory only under CONFIG_CRUX_MGMT_STATUS, which needs CONFIG_CRUX_ACCESS,
# selects the Configuration core, and refuses an image without Crux System and Crux Netif, whose sources it
# builds. The two variants that omit the credential and event items (source_no_*.c) are not built: this
# selection has both.
#
# The snapshot is refreshed at most every second, as the Host build does by default.
#
# No crux:: target and no public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are
# the ones the Host build lists in components/mgmt_status/CMakeLists.txt, less the two variants above, and
# tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_MGMT_STATUS_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_MGMT_STATUS_COMPONENT_DIR}/.." ABSOLUTE)

if(NOT CONFIG_CRUX_MGMT_STATUS)
  message(FATAL_ERROR "This directory is the status service selection of the module: set CONFIG_CRUX_MGMT_STATUS")
endif()

add_library(crux_mgmt_status_zephyr STATIC
  ../../src/mgmt_status.c
  ../../src/mgmt_status_json.c
  ../../src/source_system.c
  ../../src/source_time.c
  ../../src/source_netif.c
  ../../src/source_credential.c
  ../../src/source_security_events.c
)
target_include_directories(crux_mgmt_status_zephyr PRIVATE
  "${CRUX_MGMT_STATUS_COMPONENT_DIR}/include"
  "${CRUX_MGMT_STATUS_COMPONENT_DIR}/src"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/configuration/include"
  "${CRUX_COMPONENTS_DIR}/credential/include"
  "${CRUX_COMPONENTS_DIR}/security_event/include")
target_compile_definitions(crux_mgmt_status_zephyr PRIVATE CRUX_MGMT_STATUS_REFRESH_MS=1000u)
zephyr_append_cmake_library(crux_mgmt_status_zephyr)
target_link_libraries(crux_mgmt_status_zephyr PRIVATE zephyr_interface crux::base crux::sync crux::system
  crux::time crux::netif)
add_dependencies(crux_mgmt_status_zephyr zephyr_generated_headers)
target_compile_options(crux_mgmt_status_zephyr PRIVATE -Wall -Wextra -Werror)
