# RTT build feasibility only. Never flash, reset or access a physical board.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spike103_rtt_evkb LANGUAGES C)
if(NOT CONFIG_BOARD_MIMXRT1060_EVK OR NOT BOARD_REVISION STREQUAL "B")
  message(FATAL_ERROR "Issue #103 RTT probe requires EVKB revision B")
endif()
target_sources(app PRIVATE main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
