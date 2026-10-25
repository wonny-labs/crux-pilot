cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_system_evkb LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxConsumerBoundary.cmake")
include(../consumers.cmake)
target_sources(app PRIVATE main.c)
target_include_directories(app PRIVATE ../src ../../../samples/controller/system ../../../samples/controller/include)
target_link_libraries(app PRIVATE system_runtime system_controller crux::system)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# Retain all six public operations for representative resource accounting without
# executing irreversible start/clear/reset operations at boot.
foreach(operation crux_system_status_get crux_system_reset_cause_get
        crux_system_reset_cause_clear crux_system_reset crux_watchdog_start crux_watchdog_feed)
    target_link_options(app PUBLIC -Wl,--undefined=${operation})
endforeach()
