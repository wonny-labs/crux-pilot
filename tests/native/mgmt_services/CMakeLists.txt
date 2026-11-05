# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_mgmt_services_native LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This suite is restricted to native_sim, and it operates no hardware")
endif()
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# The real System, Time and Sync through their Zephyr providers. Access has no Zephyr composition
# yet, so a small double stands in for it, and the store is the fake provider.
target_sources(app PRIVATE src/main.c
  ${CRUX_ROOT}/components/mgmt_status/src/mgmt_status.c
  ${CRUX_ROOT}/components/mgmt_status/src/mgmt_status_json.c
  ${CRUX_ROOT}/components/mgmt_status/src/source_system.c
  ${CRUX_ROOT}/components/mgmt_status/src/source_time.c
  ${CRUX_ROOT}/components/mgmt_status/src/source_no_credential.c
  ${CRUX_ROOT}/components/mgmt_status/src/source_no_security_events.c
  ${CRUX_ROOT}/components/config_service/src/config_service.c
  ${CRUX_ROOT}/components/configuration/src/configuration.c
  ${CRUX_ROOT}/components/configuration/src/configuration_text.c
  ${CRUX_ROOT}/tests/configuration/fault_provider.c)
target_include_directories(app PRIVATE
  ${CRUX_ROOT}/components/access/include
  ${CRUX_ROOT}/components/configuration/include
  ${CRUX_ROOT}/components/configuration/src
  ${CRUX_ROOT}/components/config_service/include
  ${CRUX_ROOT}/components/config_service/src
  ${CRUX_ROOT}/components/mgmt_status/include
  ${CRUX_ROOT}/components/mgmt_status/src
  ${CRUX_ROOT}/tests/configuration)
target_link_libraries(app PRIVATE crux::system crux::sync crux::time)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# Only the physical effects are doubled, as in tests/system/zephyr.
# crux_system_status_get is only counted, and goes on to the real one.
foreach(symbol z_impl_hwinfo_get_reset_cause z_impl_hwinfo_get_supported_reset_cause
        z_impl_hwinfo_clear_reset_cause crux_system_status_get)
  target_link_options(app PUBLIC -Wl,--wrap=${symbol})
endforeach()
