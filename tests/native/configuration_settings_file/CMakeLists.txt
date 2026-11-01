# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_configuration_settings_file C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_SETTINGS_FILE)
  message(FATAL_ERROR "This fixture is restricted to native_sim with Settings File")
endif()
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(PROVIDER_COPY "${CMAKE_CURRENT_BINARY_DIR}/configuration_settings_probe.c")
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/prepare_provider.py
  ${CRUX_ROOT}/components/configuration/ports/zephyr/configuration_settings.c ${PROVIDER_COPY}
  RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Could not prepare simulator-only provider copy")
endif()
target_sources(app PRIVATE main.c ${PROVIDER_COPY}
  ${CRUX_ROOT}/components/configuration/src/configuration.c)
target_include_directories(app PRIVATE ${CRUX_ROOT}/components/configuration/include
  ${CRUX_ROOT}/components/configuration/src ${CRUX_ROOT}/components/configuration/ports/zephyr)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# Negative control: -DCRUX_SETTINGS_FILE_CONTROL=ON keeps the unpatched upstream backend, so the
# suite must fail. Never a product configuration.
if(NOT CRUX_SETTINGS_FILE_CONTROL)
  include(${CMAKE_CURRENT_LIST_DIR}/candidate.cmake)
endif()
target_link_options(app PUBLIC -Wl,--wrap=fs_read -Wl,--wrap=fs_write -Wl,--wrap=fs_open
  -Wl,--wrap=fs_close)
