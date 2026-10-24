# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_maintenance_storage C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_FLASH_SIMULATOR)
  message(FATAL_ERROR "Maintenance composition is simulator-only")
endif()
option(CRUX_MAINTENANCE_GATE "Enable private one-way lifecycle (OFF is transparent negative control)" ON)
if(CRUX_MAINTENANCE_GATE)
  set(_maintenance maintenance.c)
else()
  configure_file(evidence/transparent-maintenance.c.txt ${CMAKE_BINARY_DIR}/transparent-maintenance.c COPYONLY)
  set(_maintenance ${CMAKE_BINARY_DIR}/transparent-maintenance.c)
endif()
target_sources(app PRIVATE main.c ${_maintenance} ../settings_readiness_gate/gate.c
  ${CRUX_ROOT}/components/configuration/src/configuration.c
  ${CRUX_ROOT}/components/configuration/ports/zephyr/configuration_settings.c)
target_link_libraries(app PRIVATE crux::file)
target_include_directories(app PRIVATE . ../settings_readiness_gate
  ${CRUX_ROOT}/components/configuration/include ${CRUX_ROOT}/components/configuration/src
  ${CRUX_ROOT}/components/configuration/ports/zephyr ${CRUX_ROOT}/components/file/src
  ${CRUX_ROOT}/components/file/ports/zephyr ${ZEPHYR_BASE}/subsys/settings/include)
target_compile_definitions(app PRIVATE CRUX_CONFIGURATION_SETTINGS_SIMULATOR=1)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
include(${CMAKE_CURRENT_LIST_DIR}/../settings_target_candidate/provider/candidate.cmake)
zephyr_link_libraries(-Wl,--wrap=flash_area_open -Wl,--wrap=crux_config_zephyr_init
  -Wl,--wrap=crux_config_port_read -Wl,--wrap=crux_config_port_write
  -Wl,--wrap=crux_file_port_open -Wl,--wrap=crux_file_port_truncate
  -Wl,--wrap=crux_file_port_read -Wl,--wrap=crux_file_port_write
  -Wl,--wrap=crux_file_port_seek -Wl,--wrap=crux_file_port_sync
  -Wl,--wrap=crux_file_port_close -Wl,--wrap=crux_file_port_remove
  -Wl,--wrap=fs_close -Wl,--wrap=fs_sync -Wl,--wrap=fs_write
  -Wl,--wrap=settings_subsys_init -Wl,--wrap=settings_save_one
  -Wl,--wrap=settings_load_subtree_direct
  -Wl,--wrap=flash_area_write -Wl,--wrap=flash_area_erase -Wl,--wrap=flash_area_flatten)
