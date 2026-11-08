cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_gpio_physical C)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CruxConsumerBoundary.cmake")
# The portable consumer is the one the board cross-build and native_sim tests already use.
add_library(gpio_consumer_portable OBJECT ../gpio/consumer.c)
target_link_libraries(gpio_consumer_portable PRIVATE crux::gpio)
target_compile_options(gpio_consumer_portable PRIVATE -Wall -Wextra -Werror)
set_target_properties(gpio_consumer_portable PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES
    C_EXTENSIONS OFF)
crux_assert_portable_consumer(gpio_consumer_portable)
# The build identity is printed so a log can be tied to an exact commit (physical plan, rule 1).
execute_process(COMMAND git -C "${CMAKE_CURRENT_LIST_DIR}" describe --always --dirty
    OUTPUT_VARIABLE CRUX_P1_BUILD_ID OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT CRUX_P1_BUILD_ID)
  set(CRUX_P1_BUILD_ID "unknown")
endif()
target_sources(app PRIVATE main.c $<TARGET_OBJECTS:gpio_consumer_portable>)
target_include_directories(app PRIVATE ../gpio)
target_compile_definitions(app PRIVATE CRUX_P1_BUILD_ID="${CRUX_P1_BUILD_ID}")
target_link_libraries(app PRIVATE crux::gpio)
