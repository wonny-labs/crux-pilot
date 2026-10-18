cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_m3_native_tests LANGUAGES C)
target_sources(app PRIVATE src/main.c src/fault_gpio.c
    ../../samples/controller/core/health.c
    ../../samples/controller/zephyr/device_input.c)
target_include_directories(app PRIVATE ../../samples/controller/include
    ../../samples/controller/zephyr .)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
