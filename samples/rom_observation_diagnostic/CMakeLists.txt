cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rom_observation_diagnostic LANGUAGES C)
if(NOT CONFIG_BOARD_TARGET STREQUAL "mimxrt1060_evk@B/mimxrt1062/qspi")
    message(FATAL_ERROR "Diagnostic is restricted to the pinned EVKB B QSPI configuration")
endif()
# Fail closed for accidental persistence, reset-provider or service selection.
if(CONFIG_FLASH OR CONFIG_FILE_SYSTEM OR CONFIG_SETTINGS OR CONFIG_NVS OR
   CONFIG_WATCHDOG OR CONFIG_REBOOT OR CONFIG_HWINFO OR CONFIG_CRUX_SYSTEM OR
   CONFIG_NETWORKING OR CONFIG_DMA OR CONFIG_USB_DEVICE_STACK OR CONFIG_SHELL OR
   CONFIG_CONSOLE OR CONFIG_LOG OR CONFIG_PRINTK OR CONFIG_FPU)
    message(FATAL_ERROR "Diagnostic composition includes a forbidden provider/service")
endif()
set(candidate_private "${CMAKE_CURRENT_LIST_DIR}/../flash_target_candidate")
include(${candidate_private}/ram_build.cmake)
# Separate output directory, exact same accepted pin checks/replacement mechanism.
set(poc_generated "${CMAKE_CURRENT_BINARY_DIR}/rom-observation-diagnostic")
execute_process(COMMAND ${PYTHON_EXECUTABLE}
    "${poc_repo}/tests/native/rom_observation_diagnostic/stage.py"
    "${poc_workspace}" "${poc_generated}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${PYTHON_EXECUTABLE}
    "${poc_repo}/tests/native/flash_target_candidate/stage_fcb.py"
    "${poc_workspace}" "${poc_generated}" COMMAND_ERROR_IS_FATAL ANY)
poc_replace(drivers__memc "${ZEPHYR_BASE}/drivers/memc/memc_mcux_flexspi.c")
poc_replace(zephyr "${ZEPHYR_BASE}/soc/nxp/imxrt/imxrt10xx/flexspi.c")
poc_replace(modules__hal_nxp "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c")
poc_replace(boards__nxp__mimxrt1060_evk "${ZEPHYR_BASE}/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.c")
set(diagnostic_ram
    "${poc_generated}/observation_native.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/observation.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/terminal.c"
    "${candidate_private}/rom_deadline.c")
target_sources(drivers__memc PRIVATE ${diagnostic_ram})
target_include_directories(drivers__memc PRIVATE "${candidate_private}"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/gpt"
    "${ZEPHYR_BASE}/drivers/flash" "${ZEPHYR_BASE}/drivers/memc")
target_include_directories(modules__hal_nxp PRIVATE "${candidate_private}")
foreach(source IN LISTS diagnostic_ram)
    zephyr_code_relocate(FILES "${source}" LOCATION ITCM_TEXT)
    zephyr_code_relocate(FILES "${source}" LOCATION DTCM_DATA_BSS_RODATA)
endforeach()
target_sources(app PRIVATE main.c runtime.c
    "${poc_generated}/rom_profile.c"
    "${candidate_private}/early_snapshot.S" "${candidate_private}/snapshot_layout.c"
    "${poc_generated}/island.c" "${candidate_private}/island_context.S"
    "${candidate_private}/early_clock.S")
target_include_directories(app PRIVATE "${candidate_private}"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/gpt")
set_property(SOURCE "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/system_MIMXRT1062.c"
    TARGET_DIRECTORY modules__hal_nxp APPEND PROPERTY COMPILE_DEFINITIONS DISABLE_WDOG=0)
zephyr_link_libraries(-Wl,--wrap=soc_reset_hook -Wl,--wrap=soc_early_init_hook)
zephyr_linker_sources(SECTIONS "${candidate_private}/snapshot.ld"
    "${candidate_private}/island.ld" "${candidate_private}/firmware_limit.ld")
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${poc_repo}/tests/native/rom_observation_diagnostic/stage.py"
    "${poc_repo}/tests/native/flash_target_candidate/stage_commands.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/observation_nor.inc"
    "${candidate_private}/rom_command_memc.inc" "${candidate_private}/rom_identity_memc.inc"
    "${candidate_private}/rom_deadline.h"
    "${candidate_private}/island.c" "${candidate_private}/rom_profile.c"
    "${ZEPHYR_BASE}/soc/nxp/imxrt/imxrt10xx/flexspi.c")
add_custom_target(observation_image_guard ALL
    COMMAND ${PYTHON_EXECUTABLE} "${poc_repo}/tests/native/storage_target_guard/verify_image.py"
        --elf "${PROJECT_BINARY_DIR}/zephyr/zephyr.elf"
        --bin "${PROJECT_BINARY_DIR}/zephyr/zephyr.bin"
    DEPENDS zephyr_final VERBATIM)
