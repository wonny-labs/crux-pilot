cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_m5_physical_serial_system LANGUAGES C)

target_sources(app PRIVATE main.c)
target_link_libraries(app PRIVATE crux::time crux::logging crux::serial crux::system)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
