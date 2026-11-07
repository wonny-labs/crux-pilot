cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_storage_emmc C)
# An unsatisfied Kconfig dependency only warns, which would build an image without the provider.
if(NOT CONFIG_CRUX_FILE_RELIANCE_EDGE)
  message(FATAL_ERROR "The Reliance Edge File provider is not selected: it needs the optional "
    "west group crux-reliance")
endif()
target_sources(app PRIVATE main.c emmc_storage.c)
target_link_libraries(app PRIVATE crux::file)
target_include_directories(app PRIVATE ${CRUX_ROOT}/zephyr/fs/reliance_edge
  ${CRUX_ROOT}/components/file/ports/zephyr)
