add_executable(crux_thread_sleep_tests common.c
    ../../components/thread/src/thread_sleep.c ../../components/time/src/time_clock.c
    ../../components/time/src/time_types.c)
target_include_directories(crux_thread_sleep_tests PRIVATE
    ../../components/thread/src ../../components/time/src ../../components/interrupt/src)
target_link_libraries(crux_thread_sleep_tests PRIVATE crux::thread)
target_compile_options(crux_thread_sleep_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Thread.TypedSleep COMMAND crux_thread_sleep_tests)
set_tests_properties(Thread.TypedSleep PROPERTIES TIMEOUT 5)

add_executable(crux_thread_linux_sleep_tests linux.c)
target_link_libraries(crux_thread_linux_sleep_tests PRIVATE crux::thread)
target_compile_options(crux_thread_linux_sleep_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_thread_linux_sleep_tests PRIVATE
    -Wl,--wrap=clock_gettime -Wl,--wrap=clock_getres -Wl,--wrap=clock_nanosleep)
add_test(NAME Thread.TypedLinuxSleep COMMAND crux_thread_linux_sleep_tests)
set_tests_properties(Thread.TypedLinuxSleep PROPERTIES TIMEOUT 5)

add_library(crux_thread_clock_integration_portable OBJECT clock_integration.cpp)
target_link_libraries(crux_thread_clock_integration_portable PRIVATE crux::thread crux::time)
include(../../cmake/CruxConsumerBoundary.cmake)
crux_assert_portable_consumer(crux_thread_clock_integration_portable)
add_executable(crux_thread_clock_integration $<TARGET_OBJECTS:crux_thread_clock_integration_portable>)
target_link_libraries(crux_thread_clock_integration PRIVATE crux::thread crux::time)
add_test(NAME Thread.TypedLinuxIntegration COMMAND crux_thread_clock_integration)
set_tests_properties(Thread.TypedLinuxIntegration PROPERTIES TIMEOUT 5)
