cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_native_integrated_output LANGUAGES C)
set(sample ${CMAKE_CURRENT_SOURCE_DIR}/../../../samples/controller/zephyr)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${sample}/m3_main.c ${sample}/network_client.c ${CMAKE_CURRENT_SOURCE_DIR}/extract_formats.py)
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_formats.py
    ${sample} ${CMAKE_CURRENT_BINARY_DIR}/production_formats.h COMMAND_ERROR_IS_FATAL ANY)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${sample})
target_sources(app PRIVATE src/main.c ${sample}/native_logger.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
if(CRUX_TEST_LOSS)
    target_compile_definitions(app PRIVATE CRUX_TEST_LOSS=1)
endif()
