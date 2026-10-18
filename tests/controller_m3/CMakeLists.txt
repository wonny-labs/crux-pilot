cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_m3_native_tests LANGUAGES C)
if(NOT M3_GPIO_SCENARIO MATCHES "^(emulated|missing|fault)$")
    message(FATAL_ERROR "Select an explicit M3_GPIO_SCENARIO")
endif()
target_compile_definitions(app PRIVATE
    M3_EXPECT_MISSING=$<STREQUAL:${M3_GPIO_SCENARIO},missing>
    M3_EXPECT_READ_ERROR=$<STREQUAL:${M3_GPIO_SCENARIO},fault>)
target_sources(app PRIVATE src/main.c src/fault_gpio.c
    ../../samples/controller/core/health.c
    ../../samples/controller/zephyr/device_input.c)
target_include_directories(app PRIVATE ../../samples/controller/include
    ../../samples/controller/zephyr .)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
