include(GoogleTest)

include(${PROJECT_SOURCE_DIR}/components/time/sources.cmake)

add_library(crux_time_common_test STATIC ${CRUX_TIME_COMMON_SOURCES})
target_compile_definitions(crux_time_common_test PRIVATE CRUX_TIME_TESTING=1)
target_include_directories(crux_time_common_test
    PUBLIC ${PROJECT_SOURCE_DIR}/components/time/include
    PRIVATE ${PROJECT_SOURCE_DIR}/components/time/src
)
target_link_libraries(crux_time_common_test PUBLIC crux::base)
target_compile_options(crux_time_common_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_time_memory_test STATIC ${PROJECT_SOURCE_DIR}/components/time/src/time_port_memory.c)
target_compile_definitions(crux_time_memory_test PRIVATE CRUX_TIME_TESTING=1)
target_include_directories(crux_time_memory_test PRIVATE
    ${PROJECT_SOURCE_DIR}/components/time/include
    ${PROJECT_SOURCE_DIR}/components/time/src
)
target_link_libraries(crux_time_memory_test PRIVATE crux::base)
target_compile_options(crux_time_memory_test PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_time_test INTERFACE)
target_link_libraries(crux_time_test INTERFACE crux_time_common_test crux_time_memory_test)

add_executable(crux_time_tests
    time_test.cpp
    calendar_test.cpp
    time_types_test.cpp
    time_types_c_smoke.c
    command_test.cpp
)
target_include_directories(crux_time_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(crux_time_tests
    PRIVATE crux_time_test crux_time_commands GTest::gtest_main
)
target_compile_options(crux_time_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
gtest_discover_tests(crux_time_tests DISCOVERY_TIMEOUT 30 PROPERTIES TIMEOUT 30)

add_executable(crux_time_clock_tests clock_test.c
    ../../components/time/src/time_clock.c ../../components/time/src/time_types.c)
target_include_directories(crux_time_clock_tests PRIVATE
    ../../components/time/include ../../components/time/src)
target_link_libraries(crux_time_clock_tests PRIVATE crux::base)
target_compile_options(crux_time_clock_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Time.TypedClock COMMAND crux_time_clock_tests)
set_tests_properties(Time.TypedClock PROPERTIES TIMEOUT 5)

add_executable(crux_time_clock_linux_tests clock_linux_test.c)
target_link_libraries(crux_time_clock_linux_tests PRIVATE crux::time)
target_link_options(crux_time_clock_linux_tests PRIVATE
    -Wl,--wrap=clock_gettime -Wl,--wrap=clock_getres)
target_compile_options(crux_time_clock_linux_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Time.TypedLinuxClock COMMAND crux_time_clock_linux_tests)
set_tests_properties(Time.TypedLinuxClock PROPERTIES TIMEOUT 5)
