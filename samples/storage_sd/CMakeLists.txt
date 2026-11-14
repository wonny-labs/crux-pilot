cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_storage_sd C)
# An unsatisfied Kconfig dependency only warns, which would build an image without the provider.
if(NOT CONFIG_CRUX_FILE_FATFS_RT1060)
  message(FATAL_ERROR "The RT1060 File provider is not selected: it requires the Rev B QSPI board")
endif()
target_sources(app PRIVATE main.c sd_storage.c sd_file_check.c)
# The root binding is a composition interface: this sample is a platform composition, so it links the
# public composition target instead of adding the port's directory.
target_link_libraries(app PRIVATE crux::file crux::file_zephyr)
