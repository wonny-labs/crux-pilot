# Issue #103: use actual native Zephyr backends, not a Crux logging replacement.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(spike103_native_logging LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_FLASH_SIMULATOR)
  message(FATAL_ERROR "Issue #103 Logging SPIKE requires simulated Flash")
endif()
target_sources(app PRIVATE main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# Count native FS backend writes/syncs without changing the Zephyr source tree.
target_link_options(app PRIVATE -Wl,--wrap=fs_write -Wl,--wrap=fs_sync)
