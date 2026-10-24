# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_settings_readiness_gate C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_FLASH_SIMULATOR)
  message(FATAL_ERROR "Configuration candidate is simulator-only")
endif()
set(CRUX_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
option(CRUX_READINESS_GATE "Enable private readiness composition (OFF reproduces red baseline)" ON)
if(CRUX_READINESS_GATE)
  set(_gate_source gate.c)
else()
  configure_file(evidence/red-gate.c.txt ${CMAKE_BINARY_DIR}/red-gate.c COPYONLY)
  set(_gate_source ${CMAKE_BINARY_DIR}/red-gate.c)
endif()
target_sources(app PRIVATE main.c ${_gate_source} ${CRUX_ROOT}/components/configuration/src/configuration.c
  ${CRUX_ROOT}/components/configuration/ports/zephyr/configuration_settings.c)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${CRUX_ROOT}/components/configuration/include
  ${CRUX_ROOT}/components/configuration/src ${CRUX_ROOT}/components/configuration/ports/zephyr
  ${ZEPHYR_BASE}/subsys/settings/include)
target_compile_definitions(app PRIVATE CRUX_CONFIGURATION_SETTINGS_SIMULATOR=1)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
include(${CMAKE_CURRENT_LIST_DIR}/../settings_target_candidate/provider/candidate.cmake)

zephyr_link_libraries(-Wl,--wrap=settings_load_subtree_direct -Wl,--wrap=settings_save_one -Wl,--wrap=settings_subsys_init -Wl,--wrap=flash_area_open -Wl,--wrap=crux_config_zephyr_init -Wl,--wrap=crux_config_port_read -Wl,--wrap=crux_config_port_write)
