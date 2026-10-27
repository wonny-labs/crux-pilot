cmake_minimum_required(VERSION 3.28)
get_filename_component(CRUX_BUSY_WAIT_REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET crux::base)
    project(crux_time_busy_wait_tests LANGUAGES C)
    add_subdirectory("${CRUX_BUSY_WAIT_REPO}/components/base" crux-base)
endif()
enable_testing()
add_executable(crux_time_busy_wait_common_tests common.c
    "${CRUX_BUSY_WAIT_REPO}/components/time/src/busy_wait.c")
target_link_libraries(crux_time_busy_wait_common_tests PRIVATE crux::base)
target_include_directories(crux_time_busy_wait_common_tests PRIVATE
    "${CRUX_BUSY_WAIT_REPO}/components/time/include"
    "${CRUX_BUSY_WAIT_REPO}/components/time/src"
    "${CRUX_BUSY_WAIT_REPO}/components/time/include")
set_target_properties(crux_time_busy_wait_common_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(crux_time_busy_wait_common_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Time.BusyWait.Common COMMAND crux_time_busy_wait_common_tests)

foreach(profile IN ITEMS stable native_sim noclock runtime_read dynamic custom smp)
    add_executable(crux_time_busy_wait_zephyr_${profile}_tests zephyr_port_test.c
        "${CRUX_BUSY_WAIT_REPO}/components/time/src/busy_wait.c"
        "${CRUX_BUSY_WAIT_REPO}/components/time/ports/zephyr/busy_wait_zephyr.c")
    target_link_libraries(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE crux::base)
    target_include_directories(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE
        zephyr_shim "${CRUX_BUSY_WAIT_REPO}/components/time/include"
        "${CRUX_BUSY_WAIT_REPO}/components/time/src" "${CRUX_BUSY_WAIT_REPO}/components/time/include")
    set_target_properties(crux_time_busy_wait_zephyr_${profile}_tests PROPERTIES
        C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOT profile STREQUAL "noclock")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE CONFIG_SYS_CLOCK_EXISTS=1)
    endif()
    if(profile STREQUAL "stable" OR profile STREQUAL "native_sim")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE EXPECT_SUPPORTED=1)
    else()
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE EXPECT_SUPPORTED=0)
    endif()
    if(profile STREQUAL "native_sim")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE
            CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT=1 CONFIG_BOARD_NATIVE_SIM=1)
    elseif(profile STREQUAL "runtime_read")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE
            CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME=1)
    elseif(profile STREQUAL "dynamic")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE
            CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE=1)
    elseif(profile STREQUAL "custom")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT=1)
    elseif(profile STREQUAL "smp")
        target_compile_definitions(crux_time_busy_wait_zephyr_${profile}_tests PRIVATE CONFIG_SMP=1)
    endif()
    add_test(NAME Time.BusyWait.ZephyrPort.${profile} COMMAND crux_time_busy_wait_zephyr_${profile}_tests)
endforeach()

add_executable(crux_time_busy_wait_linux_tests linux_port_test.c
    "${CRUX_BUSY_WAIT_REPO}/components/time/src/busy_wait.c"
    "${CRUX_BUSY_WAIT_REPO}/components/time/ports/linux/busy_wait_linux.c")
target_link_libraries(crux_time_busy_wait_linux_tests PRIVATE crux::base)
target_include_directories(crux_time_busy_wait_linux_tests PRIVATE
    "${CRUX_BUSY_WAIT_REPO}/components/time/include" "${CRUX_BUSY_WAIT_REPO}/components/time/src"
    "${CRUX_BUSY_WAIT_REPO}/components/time/include")
target_compile_definitions(crux_time_busy_wait_linux_tests PRIVATE _POSIX_C_SOURCE=200809L)
set_target_properties(crux_time_busy_wait_linux_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(crux_time_busy_wait_linux_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_time_busy_wait_linux_tests PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=clock_getres
    -Wl,--wrap=malloc -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=nanosleep
    -Wl,--wrap=clock_nanosleep -Wl,--wrap=sched_yield)
add_test(NAME Time.BusyWait.LinuxPort COMMAND crux_time_busy_wait_linux_tests)
set_tests_properties(Time.BusyWait.LinuxPort PROPERTIES TIMEOUT 10)

if(TARGET crux::time AND CRUX_TIME_BUSY_WAIT)
    add_executable(crux_time_busy_wait_linux_integration linux_integration.c)
    target_link_libraries(crux_time_busy_wait_linux_integration PRIVATE crux::time)
    target_compile_options(crux_time_busy_wait_linux_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
    include("${CRUX_BUSY_WAIT_REPO}/cmake/CruxConsumerBoundary.cmake")
    crux_assert_portable_consumer(crux_time_busy_wait_linux_integration)
    add_test(NAME Time.BusyWait.LinuxIntegration COMMAND crux_time_busy_wait_linux_integration)
    set_tests_properties(Time.BusyWait.LinuxIntegration PROPERTIES TIMEOUT 10)
endif()

add_executable(crux_time_busy_wait_disabled_tests disabled.c
    "${CRUX_BUSY_WAIT_REPO}/components/time/src/busy_wait.c"
    "${CRUX_BUSY_WAIT_REPO}/components/time/src/busy_wait_disabled.c")
target_include_directories(crux_time_busy_wait_disabled_tests PRIVATE
    "${CRUX_BUSY_WAIT_REPO}/components/time/include"
    "${CRUX_BUSY_WAIT_REPO}/components/time/src")
target_link_libraries(crux_time_busy_wait_disabled_tests PRIVATE crux::base)
target_compile_options(crux_time_busy_wait_disabled_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Time.BusyWait.Disabled COMMAND crux_time_busy_wait_disabled_tests)
