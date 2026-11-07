# SPDX-License-Identifier: Apache-2.0
# Build-local correction of the MMC stack; never modify the pinned dependency checkout.
get_filename_component(crux_mmc_repo "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(crux_mmc_original "${ZEPHYR_BASE}/subsys/sd/mmc.c")
set(crux_mmc_generated "${CMAKE_CURRENT_BINARY_DIR}/crux-mmc/mmc.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${crux_mmc_original}"
    "${crux_mmc_repo}/patches/zephyr/mmc-cache-flush.patch"
    "${crux_mmc_repo}/scripts/prepare-mmc-cache-flush.py")
execute_process(COMMAND ${PYTHON_EXECUTABLE}
    "${crux_mmc_repo}/scripts/prepare-mmc-cache-flush.py"
    "${ZEPHYR_BASE}" "${crux_mmc_generated}"
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT TARGET subsys__sd)
    message(FATAL_ERROR "Pinned SD library is unavailable")
endif()
get_target_property(crux_mmc_sources subsys__sd SOURCES)
get_target_property(crux_mmc_source_dir subsys__sd SOURCE_DIR)
set(crux_mmc_remaining "")
set(crux_mmc_replaced 0)
foreach(crux_mmc_source IN LISTS crux_mmc_sources)
    get_filename_component(crux_mmc_absolute "${crux_mmc_source}" ABSOLUTE
        BASE_DIR "${crux_mmc_source_dir}")
    if(crux_mmc_absolute STREQUAL crux_mmc_original)
        math(EXPR crux_mmc_replaced "${crux_mmc_replaced} + 1")
    else()
        list(APPEND crux_mmc_remaining "${crux_mmc_source}")
    endif()
endforeach()
if(NOT crux_mmc_replaced EQUAL 1)
    message(FATAL_ERROR "Expected exactly one pinned MMC source, found ${crux_mmc_replaced}")
endif()
set_property(TARGET subsys__sd PROPERTY SOURCES "${crux_mmc_remaining}")
target_sources(subsys__sd PRIVATE "${crux_mmc_generated}")
# The generated copy includes the stack's private headers by name.
target_include_directories(subsys__sd PRIVATE "${ZEPHYR_BASE}/subsys/sd")
set_source_files_properties("${crux_mmc_generated}" TARGET_DIRECTORY subsys__sd
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
