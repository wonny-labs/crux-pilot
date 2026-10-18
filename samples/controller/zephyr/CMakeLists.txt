cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_reference_controller LANGUAGES C)
target_sources(app PRIVATE ../core/controller.c)
if(CONFIG_CRUX_CONTROLLER_M3)
    target_sources(app PRIVATE m3_main.c device_input.c ../core/health.c)
    execute_process(COMMAND git rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../../.."
        OUTPUT_VARIABLE crux_m3_source OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    target_compile_definitions(app PRIVATE CRUX_M3_SOURCE="${crux_m3_source}")
else()
    target_sources(app PRIVATE main.c)
endif()
target_include_directories(app PRIVATE ../include)
target_link_libraries(app PRIVATE crux::time)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
