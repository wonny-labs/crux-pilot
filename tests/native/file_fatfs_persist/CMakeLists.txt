cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_file_fatfs_persist C)
target_sources(app PRIVATE main.c)
target_link_libraries(app PRIVATE crux::file)
target_include_directories(app PRIVATE ${CRUX_ROOT}/components/file/src
  ${CRUX_ROOT}/components/file/ports/zephyr)
