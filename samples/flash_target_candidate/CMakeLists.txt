cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flash_target_candidate LANGUAGES C CXX)
if(NOT CONFIG_BOARD_TARGET STREQUAL "mimxrt1060_evk@B/mimxrt1062/qspi")
    message(FATAL_ERROR "PoC is restricted to the pinned EVKB B QSPI configuration")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/ram_build.cmake)
execute_process(COMMAND ${PYTHON_EXECUTABLE} "${poc_repo}/tests/native/flash_target_candidate/prepare.py"
    "${poc_workspace}" "${poc_generated}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${PYTHON_EXECUTABLE} "${poc_repo}/tests/native/flash_target_candidate/stage_commands.py"
    "${poc_workspace}" "${poc_generated}" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND ${PYTHON_EXECUTABLE} "${poc_repo}/tests/native/flash_target_candidate/stage_fcb.py"
    "${poc_workspace}" "${poc_generated}" COMMAND_ERROR_IS_FATAL ANY)

poc_replace(drivers__flash "${ZEPHYR_BASE}/drivers/flash/flash_mcux_flexspi_nor.c")
poc_replace(drivers__memc "${ZEPHYR_BASE}/drivers/memc/memc_mcux_flexspi.c")
poc_replace(modules__hal_nxp "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c")
poc_replace(boards__nxp__mimxrt1060_evk "${ZEPHYR_BASE}/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.c")
target_sources(app PRIVATE rom_profile.c main.c early_snapshot.S snapshot_layout.c island.c island_context.S early_clock.S)
# Only this compile-only candidate changes SystemInit's disable policy.
set_property(SOURCE "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/system_MIMXRT1062.c"
    TARGET_DIRECTORY modules__hal_nxp APPEND PROPERTY COMPILE_DEFINITIONS DISABLE_WDOG=0)
zephyr_link_libraries(-Wl,--wrap=soc_reset_hook -Wl,--wrap=soc_early_init_hook)
zephyr_linker_sources(SECTIONS snapshot.ld island.ld)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# Relocation scans Zephyr library objects, not the application archive outside it.
target_sources(drivers__memc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/terminal.c")
zephyr_code_relocate(FILES "${CMAKE_CURRENT_SOURCE_DIR}/terminal.c" LOCATION ITCM_TEXT)

# This experimental envelope does not adopt the candidate layout in production.
zephyr_linker_sources(SECTIONS firmware_limit.ld)
add_custom_target(candidate_image_guard ALL
    COMMAND ${PYTHON_EXECUTABLE} "${poc_repo}/tests/native/storage_target_guard/verify_image.py"
        --elf "${PROJECT_BINARY_DIR}/zephyr/zephyr.elf"
        --bin "${PROJECT_BINARY_DIR}/zephyr/zephyr.bin"
    DEPENDS zephyr_final
    VERBATIM)

# Preserve executable lower software candidate despite refused target admission.
zephyr_link_libraries(-Wl,-u,crux_rom_command_prepare -Wl,-u,crux_rom_command_submit)
target_sources(drivers__memc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/rom_command_gate.c")
zephyr_code_relocate(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rom_command_gate.c" LOCATION ITCM_TEXT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${poc_repo}/tests/native/flash_target_candidate/stage_commands.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/rom_command_nor.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/rom_command_memc.inc"
    "${CMAKE_CURRENT_SOURCE_DIR}/rom_identity_memc.inc")

target_include_directories(drivers__memc PRIVATE
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/gpt")
# Private software timer candidate: retained for inspection, never startup-wired.
target_sources(drivers__memc PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/rom_deadline.c")
zephyr_code_relocate(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rom_deadline.c" LOCATION ITCM_TEXT)
zephyr_code_relocate(FILES "${CMAKE_CURRENT_SOURCE_DIR}/rom_deadline.c" LOCATION DTCM_DATA_BSS_RODATA)
zephyr_link_libraries(-Wl,-u,crux_rom_gpt2_candidate_init -Wl,-u,crux_rom_candidate_deadline_begin -Wl,-u,crux_rom_candidate_deadline_finish)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/rom_deadline.h"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/gpt/fsl_gpt.h"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_GPT.h"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_XTALOSC24M.h")

include(storage.cmake)
