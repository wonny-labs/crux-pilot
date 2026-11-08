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
target_link_libraries(app PRIVATE crux::file crux::file_zephyr)
# Configuration on the Settings File backend, when selected. An unsatisfied Kconfig dependency only
# warns, so the link check in scripts/test-file-emmc-compile.sh requires the symbol in .config.
if(CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE)
  target_sources(app PRIVATE emmc_configuration.c)
  target_link_libraries(app PRIVATE crux::configuration)
  # The private header of the provider (crux_config_zephyr_init), which crux::configuration does not
  # expose: the composition wires it itself.
  target_include_directories(app PRIVATE ${CRUX_ROOT}/components/configuration/ports/zephyr)
endif()
target_include_directories(app PRIVATE ${CRUX_ROOT}/zephyr/fs/reliance_edge)
