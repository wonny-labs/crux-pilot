# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_file_reliance C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This fixture operates a RAM disk on native_sim only")
endif()
target_sources(app PRIVATE main.c ${CRUX_ROOT}/tests/file/conformance.c)
target_link_libraries(app PRIVATE crux::file)
target_include_directories(app PRIVATE ${CRUX_ROOT}/zephyr/fs/reliance_edge
  ${CRUX_ROOT}/components/file/src ${CRUX_ROOT}/components/file/ports/zephyr
  ${CRUX_ROOT}/tests/file)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# Faults are injected below the file system, at the disk access calls the adapter makes.
target_link_options(app PUBLIC -Wl,--wrap=disk_access_read -Wl,--wrap=disk_access_write
  -Wl,--wrap=disk_access_ioctl)
