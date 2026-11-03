# Linux Host levels of the out-of-tree consumer: unit and integration under the
# application's own CTest, through the root add_subdirectory() entry point.
cmake_minimum_required(VERSION 3.28)
project(consumer_poc LANGUAGES C)
include(CTest)

set(CRUX_BUILD_TIME ON CACHE BOOL "Typed monotonic clock")
set(CRUX_TIME_PROVIDER memory CACHE STRING "Controlled development realtime")
set(CRUX_BUILD_THREAD ON CACHE BOOL "Absolute sleep")
add_subdirectory("${CRUX_SOURCE_DIR}" crux)

add_library(consumer_periodic STATIC src/periodic.c)
target_include_directories(consumer_periodic PUBLIC include)

add_executable(consumer_unit host/test_periodic.c)
target_link_libraries(consumer_unit PRIVATE consumer_periodic)
add_test(NAME Consumer.Unit COMMAND consumer_unit)

foreach(target IN ITEMS consumer_periodic consumer_unit)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${target} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endforeach()

add_library(consumer_app STATIC src/app_run.c)
target_include_directories(consumer_app PUBLIC include)
target_link_libraries(consumer_app PRIVATE consumer_periodic crux::time crux::thread)
crux_assert_portable_consumer(consumer_app)

add_executable(consumer_integration host/test_integration.c)
target_link_libraries(consumer_integration PRIVATE consumer_app crux::time crux::thread)
add_test(NAME Consumer.HostIntegration COMMAND consumer_integration)
set_tests_properties(Consumer.HostIntegration PROPERTIES TIMEOUT 20)

foreach(target IN ITEMS consumer_app consumer_integration)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_target_properties(${target} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
endforeach()
