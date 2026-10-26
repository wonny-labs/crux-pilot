cmake_minimum_required(VERSION 3.28)
get_filename_component(CRUX_DELAY_REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
if(NOT TARGET crux::base)
    project(crux_delay_tests LANGUAGES C)
    add_subdirectory("${CRUX_DELAY_REPO}/components/base" crux-base)
endif()
enable_testing()
add_executable(crux_delay_common_tests common.c
    "${CRUX_DELAY_REPO}/components/delay/src/delay.c")
target_link_libraries(crux_delay_common_tests PRIVATE crux::base)
target_include_directories(crux_delay_common_tests PRIVATE
    "${CRUX_DELAY_REPO}/components/delay/include"
    "${CRUX_DELAY_REPO}/components/delay/src"
    "${CRUX_DELAY_REPO}/components/time/include")
set_target_properties(crux_delay_common_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(crux_delay_common_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Delay.Common COMMAND crux_delay_common_tests)

foreach(profile IN ITEMS stable native_sim noclock runtime_read dynamic custom smp)
    add_executable(crux_delay_zephyr_${profile}_tests zephyr_port_test.c
        "${CRUX_DELAY_REPO}/components/delay/src/delay.c"
        "${CRUX_DELAY_REPO}/components/delay/ports/zephyr/delay_zephyr.c")
    target_link_libraries(crux_delay_zephyr_${profile}_tests PRIVATE crux::base)
    target_include_directories(crux_delay_zephyr_${profile}_tests PRIVATE
        zephyr_shim "${CRUX_DELAY_REPO}/components/delay/include"
        "${CRUX_DELAY_REPO}/components/delay/src" "${CRUX_DELAY_REPO}/components/time/include")
    set_target_properties(crux_delay_zephyr_${profile}_tests PROPERTIES
        C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(crux_delay_zephyr_${profile}_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(NOT profile STREQUAL "noclock")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE CONFIG_SYS_CLOCK_EXISTS=1)
    endif()
    if(profile STREQUAL "stable" OR profile STREQUAL "native_sim")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE EXPECT_SUPPORTED=1)
    else()
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE EXPECT_SUPPORTED=0)
    endif()
    if(profile STREQUAL "native_sim")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE
            CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT=1 CONFIG_BOARD_NATIVE_SIM=1)
    elseif(profile STREQUAL "runtime_read")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE
            CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME=1)
    elseif(profile STREQUAL "dynamic")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE
            CONFIG_SYSTEM_CLOCK_HW_CYCLES_PER_SEC_RUNTIME_UPDATE=1)
    elseif(profile STREQUAL "custom")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE CONFIG_ARCH_HAS_CUSTOM_BUSY_WAIT=1)
    elseif(profile STREQUAL "smp")
        target_compile_definitions(crux_delay_zephyr_${profile}_tests PRIVATE CONFIG_SMP=1)
    endif()
    add_test(NAME Delay.ZephyrPort.${profile} COMMAND crux_delay_zephyr_${profile}_tests)
endforeach()

add_executable(crux_delay_linux_tests linux_port_test.c
    "${CRUX_DELAY_REPO}/components/delay/src/delay.c"
    "${CRUX_DELAY_REPO}/components/delay/ports/linux/delay_linux.c")
target_link_libraries(crux_delay_linux_tests PRIVATE crux::base)
target_include_directories(crux_delay_linux_tests PRIVATE
    "${CRUX_DELAY_REPO}/components/delay/include" "${CRUX_DELAY_REPO}/components/delay/src"
    "${CRUX_DELAY_REPO}/components/time/include")
target_compile_definitions(crux_delay_linux_tests PRIVATE _POSIX_C_SOURCE=200809L)
set_target_properties(crux_delay_linux_tests PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_options(crux_delay_linux_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_delay_linux_tests PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=clock_getres
    -Wl,--wrap=malloc -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=nanosleep
    -Wl,--wrap=clock_nanosleep -Wl,--wrap=sched_yield)
add_test(NAME Delay.LinuxPort COMMAND crux_delay_linux_tests)
set_tests_properties(Delay.LinuxPort PROPERTIES TIMEOUT 10)

if(TARGET crux::delay)
    add_executable(crux_delay_linux_integration linux_integration.c)
    target_link_libraries(crux_delay_linux_integration PRIVATE crux::delay)
    target_compile_options(crux_delay_linux_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
    include("${CRUX_DELAY_REPO}/cmake/CruxConsumerBoundary.cmake")
    crux_assert_portable_consumer(crux_delay_linux_integration)
    add_test(NAME Delay.LinuxIntegration COMMAND crux_delay_linux_integration)
    set_tests_properties(Delay.LinuxIntegration PROPERTIES TIMEOUT 10)
endif()
