cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_system_native_sim_watchdog LANGUAGES C)
# Public targets and headers only: no wrap of a private seam, no private include path.
target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE crux::system)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
