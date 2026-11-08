# SPDX-License-Identifier: Apache-2.0
# The Settings File suite of tests/native/configuration_settings_file on Reliance Edge, against the
# target provider. main.c is that suite's source, compiled with this directory's fixture header and
# under another suite name, so that every case is the same code on both file systems. The provider
# is the real one, with the corrected Settings File backend staged by the module (zephyr/).
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_configuration_settings_reliance C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This fixture operates a RAM disk on native_sim only")
endif()
set(SHARED "${CRUX_ROOT}/tests/native/configuration_settings_file")
if(CONFIG_SETTINGS_RELIANCE_TEST_INIT_FAILURE)
  target_sources(app PRIVATE extra_cases.c)
else()
  target_sources(app PRIVATE ${SHARED}/main.c extra_cases.c)
  target_compile_definitions(app PRIVATE
    FIXTURE_HEADER="fixture_reliance.h"
    configuration_settings_file=configuration_settings_reliance)
endif()
target_sources(app PRIVATE ${CRUX_ROOT}/samples/storage_emmc/emmc_storage.c
  ${CRUX_ROOT}/samples/storage_emmc/emmc_configuration.c)
target_link_libraries(app PRIVATE crux::file crux::file_zephyr crux::configuration)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}
  ${CRUX_ROOT}/samples/storage_emmc ${CRUX_ROOT}/zephyr/fs/reliance_edge
  ${CRUX_ROOT}/components/configuration/ports/zephyr)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# The settings initialisation is counted. Faults and compactions are seen at the VFS calls the
# backend makes, as in the FatFs suite, in the scenario that builds that suite.
target_link_options(app PUBLIC -Wl,--wrap=settings_subsys_init)
if(NOT CONFIG_SETTINGS_RELIANCE_TEST_INIT_FAILURE)
  target_link_options(app PUBLIC -Wl,--wrap=fs_read -Wl,--wrap=fs_write -Wl,--wrap=fs_open
    -Wl,--wrap=fs_close)
endif()
