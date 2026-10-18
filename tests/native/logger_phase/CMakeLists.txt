cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_native_logger_phase LANGUAGES C)
set(sample ${CMAKE_CURRENT_SOURCE_DIR}/../../../samples/controller/zephyr)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${sample}/m3_main.c ${sample}/network_client.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../integrated_output/extract_formats.py)
execute_process(COMMAND ${PYTHON_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/../integrated_output/extract_formats.py
    ${sample} ${CMAKE_CURRENT_BINARY_DIR}/production_formats.h COMMAND_ERROR_IS_FATAL ANY)
file(STRINGS ${sample}/m3_main.c phase REGEX "^#define CRUX_M3_INITIAL_OBSERVER_PHASE_MS [0-9]+$")
if(NOT phase)
    message(FATAL_ERROR "Missing production observer phase")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/production_phase.h "${phase}\n")
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${sample})
target_sources(app PRIVATE src/main.c ${sample}/native_logger.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
if(CRUX_TEST_UNSHIFTED)
    target_compile_definitions(app PRIVATE CRUX_TEST_UNSHIFTED=1)
endif()
