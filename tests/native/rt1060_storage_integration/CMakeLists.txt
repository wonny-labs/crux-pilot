cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
list(APPEND DTS_ROOT "${CRUX_ROOT}/samples/flash_target_candidate")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_rt1060_storage_integration C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_FLASH_SIMULATOR)
  message(FATAL_ERROR "Controlled positive fixture requires native simulator")
endif()
set(sample "${CRUX_ROOT}/samples/flash_target_candidate")
target_sources(app PRIVATE main.c "${sample}/storage_adapter.c" "${sample}/storage_native.c"
  "${sample}/storage_trial.c" "${CRUX_ROOT}/components/configuration/src/configuration.c"
  "${CRUX_ROOT}/components/configuration/ports/zephyr/configuration_settings.c"
  "${CRUX_ROOT}/tests/native/settings_readiness_gate/gate.c"
  "${CRUX_ROOT}/tests/native/maintenance_storage/maintenance.c")
target_include_directories(app PRIVATE "${sample}" "${CRUX_ROOT}/tests/native/storage_target_guard"
  "${CRUX_ROOT}/tests/native/settings_readiness_gate" "${CRUX_ROOT}/tests/native/maintenance_storage"
  "${CRUX_ROOT}/components/configuration/include" "${CRUX_ROOT}/components/configuration/src"
  "${CRUX_ROOT}/components/configuration/ports/zephyr" "${CRUX_ROOT}/components/file/src"
  "${CRUX_ROOT}/components/file/ports/zephyr" "${ZEPHYR_BASE}/subsys/settings/include")
target_compile_definitions(app PRIVATE CRUX_CONFIGURATION_SETTINGS_SIMULATOR=1
  CRUX_GATE_FLASH_AREA_OPEN=crux_candidate_area_open CRUX_MAINTENANCE_FILE_PARTITION=candidate_file)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
target_link_libraries(app PRIVATE crux::file)
include("${CRUX_ROOT}/tests/native/settings_target_candidate/provider/candidate.cmake")
zephyr_link_libraries(-Wl,--wrap=flash_area_open -Wl,--wrap=crux_config_zephyr_init
  -Wl,--wrap=crux_config_port_read -Wl,--wrap=crux_config_port_write
  -Wl,--wrap=crux_file_port_open -Wl,--wrap=crux_file_port_truncate
  -Wl,--wrap=crux_file_port_read -Wl,--wrap=crux_file_port_write
  -Wl,--wrap=crux_file_port_seek -Wl,--wrap=crux_file_port_sync
  -Wl,--wrap=crux_file_port_close -Wl,--wrap=crux_file_port_remove
  -Wl,--wrap=flash_area_write -Wl,--wrap=flash_area_erase -Wl,--wrap=flash_area_flatten)
