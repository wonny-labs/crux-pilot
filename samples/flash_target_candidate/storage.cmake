# Exact sample-only composition. No general selector or Kconfig defaults.
if(NOT CONFIG_BOARD_TARGET STREQUAL "mimxrt1060_evk@B/mimxrt1062/qspi" OR
   NOT CONFIG_FILE_SYSTEM_LITTLEFS OR NOT CONFIG_SETTINGS_NVS OR NOT CONFIG_NVS_DATA_CRC OR
   CONFIG_SETTINGS_NVS_NAME_CACHE OR CONFIG_NVS_INIT_BAD_MEMORY_REGION OR CONFIG_USERSPACE)
  message(FATAL_ERROR "Unsupported RT1060 private storage profile")
endif()
set(CRUX_RT1060_STORAGE_SAMPLE TRUE)
add_subdirectory("${poc_repo}/components/file/ports/zephyr" "${CMAKE_CURRENT_BINARY_DIR}/candidate-file")
crux_assert_portable_usage(crux::file)
include("${poc_repo}/tests/native/settings_target_candidate/provider/candidate.cmake")
target_sources(app PRIVATE storage_adapter.c storage_native.c storage_trial.c
  "${poc_repo}/components/configuration/src/configuration.c"
  "${poc_repo}/components/configuration/ports/zephyr/configuration_settings.c"
  "${poc_repo}/tests/native/settings_readiness_gate/gate.c"
  "${poc_repo}/tests/native/maintenance_storage/maintenance.c")
target_include_directories(app PRIVATE .
  "${poc_repo}/tests/native/storage_target_guard"
  "${poc_repo}/tests/native/settings_readiness_gate"
  "${poc_repo}/tests/native/maintenance_storage"
  "${poc_repo}/components/file/src" "${poc_repo}/components/file/ports/zephyr"
  "${poc_repo}/components/configuration/src" "${poc_repo}/components/configuration/include"
  "${poc_repo}/components/configuration/ports/zephyr" "${ZEPHYR_BASE}/subsys/settings/include")
target_compile_definitions(app PRIVATE CRUX_RT1060_STORAGE_SAMPLE=1
  CRUX_GATE_FLASH_AREA_OPEN=crux_candidate_area_open
  CRUX_MAINTENANCE_FILE_PARTITION=candidate_file)
# This backend is selected after Zephyr's library collection; wire it explicitly
# in the private final composition without exposing native usage to consumers.
target_link_libraries(app PRIVATE crux::file crux_file_zephyr)
zephyr_link_libraries(-Wl,-u,crux_candidate_fixed_trial -Wl,-u,crux_rom_command_read_result
  -Wl,--wrap=flash_area_open -Wl,--wrap=crux_config_zephyr_init
  -Wl,--wrap=crux_config_port_read -Wl,--wrap=crux_config_port_write
  -Wl,--wrap=crux_file_port_open -Wl,--wrap=crux_file_port_truncate
  -Wl,--wrap=crux_file_port_read -Wl,--wrap=crux_file_port_write
  -Wl,--wrap=crux_file_port_seek -Wl,--wrap=crux_file_port_sync
  -Wl,--wrap=crux_file_port_close -Wl,--wrap=crux_file_port_remove
  -Wl,--wrap=flash_area_write -Wl,--wrap=flash_area_erase -Wl,--wrap=flash_area_flatten)
# Independent C17/C++17 consumers compile with public headers/usage only.
add_library(crux_candidate_configuration_api INTERFACE)
target_include_directories(crux_candidate_configuration_api INTERFACE "${poc_repo}/components/configuration/include")
target_compile_options(crux_candidate_configuration_api INTERFACE ${TOOLCHAIN_C_FLAGS})
foreach(language IN ITEMS c cpp)
  add_library(candidate_file_consumer_${language} OBJECT "${poc_repo}/tests/file_consumer/consumer.${language}")
  target_link_libraries(candidate_file_consumer_${language} PRIVATE crux::file)
  add_library(candidate_config_consumer_${language} OBJECT "${poc_repo}/tests/configuration_consumer/consumer.${language}")
  target_link_libraries(candidate_config_consumer_${language} PRIVATE crux_candidate_configuration_api)
  foreach(component IN ITEMS file config)
    set(consumer candidate_${component}_consumer_${language})
    set_target_properties(${consumer} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_options(${consumer} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    crux_assert_portable_consumer(${consumer})
  endforeach()
endforeach()
