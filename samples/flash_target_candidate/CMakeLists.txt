cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flash_target_candidate LANGUAGES C)
if(NOT CONFIG_BOARD_TARGET STREQUAL "mimxrt1060_evk@B/mimxrt1062/qspi")
    message(FATAL_ERROR "PoC is restricted to the pinned EVKB B QSPI configuration")
endif()
get_filename_component(poc_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(poc_workspace "${ZEPHYR_BASE}/.." ABSOLUTE)
set(poc_generated "${CMAKE_CURRENT_BINARY_DIR}/flash-target-candidate")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${poc_repo}/scripts/prepare-flash-poc.py"
    "${poc_repo}/tests/native/flash_target_candidate/prepare.py"
    "${ZEPHYR_BASE}/drivers/flash/flash_mcux_flexspi_nor.c"
    "${ZEPHYR_BASE}/drivers/memc/memc_mcux_flexspi.c"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c")
execute_process(COMMAND ${PYTHON_EXECUTABLE} "${poc_repo}/tests/native/flash_target_candidate/prepare.py"
    "${poc_workspace}" "${poc_generated}" COMMAND_ERROR_IS_FATAL ANY)
function(poc_replace target original)
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    set(remaining "")
    set(replaced 0)
    foreach(source IN LISTS sources)
        get_filename_component(absolute "${source}" ABSOLUTE BASE_DIR "${source_dir}")
        if(absolute STREQUAL original)
            math(EXPR replaced "${replaced} + 1")
        else()
            list(APPEND remaining "${source}")
        endif()
    endforeach()
    if(NOT replaced EQUAL 1)
        message(FATAL_ERROR "Expected exactly one ${original}, found ${replaced}")
    endif()
    get_filename_component(name "${original}" NAME)
    get_filename_component(directory "${original}" DIRECTORY)
    set_property(TARGET ${target} PROPERTY SOURCES "${remaining}")
    target_sources(${target} PRIVATE "${poc_generated}/${name}")
    target_include_directories(${target} PRIVATE "${directory}" "${CMAKE_CURRENT_SOURCE_DIR}" "${poc_repo}/tests/native/storage_target_guard")
    zephyr_code_relocate(FILES "${poc_generated}/${name}" LOCATION ITCM_TEXT)
    zephyr_code_relocate(FILES "${poc_generated}/${name}" LOCATION DTCM_DATA_BSS_RODATA)
endfunction()
poc_replace(drivers__flash "${ZEPHYR_BASE}/drivers/flash/flash_mcux_flexspi_nor.c")
poc_replace(drivers__memc "${ZEPHYR_BASE}/drivers/memc/memc_mcux_flexspi.c")
poc_replace(modules__hal_nxp "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c")
target_sources(app PRIVATE main.c early_snapshot.S snapshot_layout.c)
# Only this compile-only candidate changes SystemInit's disable policy.
set_property(SOURCE "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/system_MIMXRT1062.c"
    TARGET_DIRECTORY modules__hal_nxp APPEND PROPERTY COMPILE_DEFINITIONS DISABLE_WDOG=0)
zephyr_link_libraries(-Wl,--wrap=soc_reset_hook)
zephyr_linker_sources(SECTIONS snapshot.ld)
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
