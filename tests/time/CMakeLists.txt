include(GoogleTest)
find_package(Threads REQUIRED)

add_executable(crux_time_realtime_tests realtime_test.c ../../components/time/src/time.c)
target_compile_definitions(crux_time_realtime_tests PRIVATE CRUX_TIME_TESTING=1)
target_link_libraries(crux_time_realtime_tests PRIVATE crux::base Threads::Threads)
target_include_directories(crux_time_realtime_tests PRIVATE
    ../../components/time/include ../../components/time/src)
target_compile_options(crux_time_realtime_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Time.RealtimeAuthority COMMAND crux_time_realtime_tests)
set_tests_properties(Time.RealtimeAuthority PROPERTIES TIMEOUT 30)

add_executable(crux_time_calendar_fraction_tests calendar_fraction_test.c
    ../../components/time/src/time_calendar.c)
target_link_libraries(crux_time_calendar_fraction_tests PRIVATE crux::base)
target_include_directories(crux_time_calendar_fraction_tests PRIVATE ../../components/time/include)
target_compile_options(crux_time_calendar_fraction_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Time.FractionalCalendar COMMAND crux_time_calendar_fraction_tests)

add_executable(crux_time_realtime_linux_tests realtime_linux_test.c
    ../../components/time/src/time.c
    ../../components/time/ports/linux/time_realtime_linux.c
    ../../components/time/ports/linux/time_lock_linux.c)
target_link_libraries(crux_time_realtime_linux_tests PRIVATE crux::base Threads::Threads)
target_compile_definitions(crux_time_realtime_linux_tests PRIVATE _POSIX_C_SOURCE=200809L)
target_include_directories(crux_time_realtime_linux_tests PRIVATE
    ../../components/time/include ../../components/time/src)
target_compile_options(crux_time_realtime_linux_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_time_realtime_linux_tests PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=clock_settime)
add_test(NAME Time.LinuxRealtime COMMAND crux_time_realtime_linux_tests)

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
target_link_libraries(crux_time_test INTERFACE crux_time_common_test crux_time_memory_test crux_time_lock_linux)

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

foreach(target crux_time_realtime_tests crux_time_common_test crux_time_clock_tests crux_time_realtime_linux_tests)
    target_include_directories(${target} PRIVATE ../../components/interrupt/src)
endforeach()
target_link_libraries(crux_time_realtime_linux_tests PRIVATE crux_interrupt)
target_link_libraries(crux_time_test INTERFACE crux_interrupt crux_time_busy_wait)
add_subdirectory(busy_wait)
