# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_configuration_settings C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_FLASH_SIMULATOR)
  message(FATAL_ERROR "Configuration candidate is simulator-only")
endif()
set(CRUX_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../../..)
target_sources(app PRIVATE main.c ${CRUX_ROOT}/components/configuration/src/configuration.c
  ${CRUX_ROOT}/components/configuration/ports/zephyr/configuration_settings.c)
target_include_directories(app PRIVATE ${CRUX_ROOT}/components/configuration/include
  ${CRUX_ROOT}/components/configuration/src ${CRUX_ROOT}/components/configuration/ports/zephyr
  ${ZEPHYR_BASE}/subsys/settings/include)
target_compile_definitions(app PRIVATE CRUX_CONFIGURATION_SETTINGS_SIMULATOR=1)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
option(CRUX_SETTINGS_TARGET_CANDIDATE "Select isolated narrowed Settings/NVS copies" ON)
if(CRUX_SETTINGS_TARGET_CANDIDATE)
  include(${CMAKE_CURRENT_LIST_DIR}/candidate.cmake)
endif()
