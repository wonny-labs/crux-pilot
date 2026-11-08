cmake_minimum_required(VERSION 3.20.0)
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_i2c_zephyr_test LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/i2c_emulator.c
)
target_link_libraries(app PRIVATE crux::i2c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
