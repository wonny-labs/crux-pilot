cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_storage_reliance_sd C)
# An unsatisfied Kconfig dependency only warns, which would build an image without the provider or
# the disk driver.
if(NOT CONFIG_CRUX_FILE_RELIANCE_EDGE)
  message(FATAL_ERROR "The Reliance Edge File provider is not selected: it needs the optional "
    "west group crux-reliance")
endif()
if(NOT BOARD STREQUAL "native_sim" AND NOT CONFIG_DISK_DRIVER_SDMMC)
  message(FATAL_ERROR "The microSD disk driver is not selected: this sample runs on the board's "
    "microSD slot, and only native_sim may do without the driver")
endif()
# The sources of the other samples are compiled from their own directories, unchanged. The
# shim sd_storage.h of this directory comes first in the include path and maps the microSD names that
# the append and replace sources use onto emmc_storage.h.
set(EMMC_DIR ${CRUX_ROOT}/samples/storage_emmc)
set(INTERRUPT_DIR ${CRUX_ROOT}/samples/storage_sd_interrupt)
set(REPLACE_DIR ${CRUX_ROOT}/samples/storage_sd_replace)
if(CONFIG_SAMPLE_RELIANCE_SD_FORMAT)
  # The format image has its own main and mount (format.c) and compiles none of the other sources;
  # only emmc_storage.h is shared, for the disk name and the mount point.
  target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/format.c)
else()
  target_sources(app PRIVATE ${EMMC_DIR}/emmc_storage.c)
  if(CONFIG_SAMPLE_RELIANCE_SD_APPEND_TEST)
    target_sources(app PRIVATE ${INTERRUPT_DIR}/main.c ${INTERRUPT_DIR}/sd_interrupt.c)
  elseif(CONFIG_SAMPLE_RELIANCE_SD_REPLACE_TEST)
    target_sources(app PRIVATE ${REPLACE_DIR}/main.c ${REPLACE_DIR}/sd_replace.c
      ${INTERRUPT_DIR}/sd_interrupt.c)
  else()
    # The default image and the mount check: main.c mounts only with CONFIG_SAMPLE_EMMC_START.
    target_sources(app PRIVATE ${EMMC_DIR}/main.c)
  endif()
endif()
target_link_libraries(app PRIVATE crux::file)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${EMMC_DIR} ${INTERRUPT_DIR}
  ${REPLACE_DIR} ${CRUX_ROOT}/zephyr/fs/reliance_edge ${CRUX_ROOT}/components/file/ports/zephyr)
