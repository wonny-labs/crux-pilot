cmake_minimum_required(VERSION 3.20.0)
set(DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_i2c_tmp117_zephyr_test LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/tmp117_emulator.c
    ../i2c/consumer/tmp117.c
)
target_include_directories(app PRIVATE ../i2c/consumer)
target_link_libraries(app PRIVATE crux::i2c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
