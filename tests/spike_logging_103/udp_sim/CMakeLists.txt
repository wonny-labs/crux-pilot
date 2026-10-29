# Issue #103: actual pinned Zephyr network and UART logging, simulator-only.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spike103_udp LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_NET_LOOPBACK)
  message(FATAL_ERROR "Issue #103 UDP probe requires native_sim loopback")
endif()
target_sources(app PRIVATE main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
