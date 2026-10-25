# Shared private pin checks and source-copy replacement; no provider selection.
get_filename_component(poc_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(poc_workspace "${ZEPHYR_BASE}/.." ABSOLUTE)
set(poc_generated "${CMAKE_CURRENT_BINARY_DIR}/flash-target-candidate")
# The conservative pre-clock gate depends on this exact source/mux table.
file(SHA256 "${ZEPHYR_BASE}/soc/nxp/imxrt/imxrt10xx/soc.c" poc_soc_hash)
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/drivers/fsl_clock.h" poc_clock_hash)
if(NOT poc_soc_hash STREQUAL "e4f89386bc54c4d709e8fc85257421b2e36ada6adf93add19f9de5c52922d1dc" OR
   NOT poc_clock_hash STREQUAL "a1cd1e43ae5134dec53b86ef3ad9f80bd79fd02d2be536e8a3dde6c3115f33a7")
    message(FATAL_ERROR "candidate pre-clock source audit does not match pinned input")
endif()
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/drivers/fsl_clock.c" poc_clock_c_hash)
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_FLEXSPI.h" poc_flexspi_regs_hash)
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_CCM.h" poc_ccm_regs_hash)
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_CCM_ANALOG.h" poc_analog_regs_hash)
if(NOT poc_ccm_regs_hash STREQUAL "9929cf0e6194ce4191978985fdfed382907f5250a60a7fa38a60a83864703c96" OR
   NOT poc_analog_regs_hash STREQUAL "61e4bf73b4997c900a522e7a0d5f6b6a1320ed199100f8c54d96e21e9e78df86")
    message(FATAL_ERROR "candidate clock register audit does not match pinned input")
endif()
if(NOT poc_clock_c_hash STREQUAL "91200830fe9b4d290e25c169b3612f3256339852851da56dbfc01d34ec0521ed" OR
   NOT poc_flexspi_regs_hash STREQUAL "b48c5134a32e42f45180321b556b0cf227495efac145d80bd42c1a37dc9003ab")
    message(FATAL_ERROR "candidate read-confirm getter/register audit does not match pinned input")
endif()
# Software deadline audit uses the exact pinned SDK inline/register definitions.
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/gpt/fsl_gpt.h" poc_gpt_hash)
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_GPT.h" poc_gpt_regs_hash)
file(SHA256 "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_XTALOSC24M.h" poc_xtal_regs_hash)
if(NOT poc_gpt_hash STREQUAL "72a6b57bb5bd8f7854103222c3398624af45d798f4c59fde8cf0127f50d10c5d" OR
   NOT poc_gpt_regs_hash STREQUAL "b04b2d123122cfa879cc241075098800734c796f2a9d6282265e41a769936174" OR
   NOT poc_xtal_regs_hash STREQUAL "77909fc57ef79d4916b7dfb0db7a73aac0e8bd563a643c9f697dc907a38ddbd5")
    message(FATAL_ERROR "candidate GPT2 source/register audit does not match pinned input")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ZEPHYR_BASE}/soc/nxp/imxrt/imxrt10xx/soc.c"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/drivers/fsl_clock.h"
    "${poc_repo}/scripts/prepare-flash-poc.py"
    "${poc_repo}/tests/native/flash_target_candidate/prepare.py"
    "${poc_repo}/tests/native/flash_target_candidate/stage_fcb.py"
    "${ZEPHYR_BASE}/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.c"
    "${ZEPHYR_BASE}/boards/nxp/mimxrt1060_evk/xip/evkbmimxrt1060_flexspi_nor_config.h"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/MIMXRT1062/drivers/fsl_clock.c"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_FLEXSPI.h"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_CCM.h"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/devices/RT/RT1060/periph/PERI_CCM_ANALOG.h"
    "${ZEPHYR_BASE}/drivers/flash/flash_mcux_flexspi_nor.c"
    "${ZEPHYR_BASE}/drivers/memc/memc_mcux_flexspi.c"
    "${poc_workspace}/modules/hal/nxp/mcux/mcux-sdk-ng/drivers/flexspi/fsl_flexspi.c")
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
    if(target STREQUAL "boards__nxp__mimxrt1060_evk")
        return() # FCB stays in its Boot ROM section; it is not RAM code.
    endif()
    zephyr_code_relocate(FILES "${poc_generated}/${name}" LOCATION ITCM_TEXT)
    zephyr_code_relocate(FILES "${poc_generated}/${name}" LOCATION DTCM_DATA_BSS_RODATA)
endfunction()
