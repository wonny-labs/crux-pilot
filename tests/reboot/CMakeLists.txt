# The reboot service on the real Access core, over fake ports for the reset, the sleep, the events that
# are still pending and the event record. All of these exist only when Crux builds its own tests. The
# sleep is virtual, so a delay of 1 s and a wait of 15 s cost no time and the bounds are held to exact
# numbers.
#
# The service is Zephyr-direct (pilot of ADR-0002 P9), so its core is built here over a test-only stub of
# the kernel calls that it uses (host_kernel). The same suites run on native_sim over the real kernel,
# in tests/reboot/zephyr; that is where the kernel's own behaviour is held to account.
find_package(Threads REQUIRED)

add_library(reboot_host_kernel STATIC host_kernel/kernel_stub.c)
target_include_directories(reboot_host_kernel PUBLIC host_kernel)
target_link_libraries(reboot_host_kernel PUBLIC Threads::Threads)
target_compile_options(reboot_host_kernel PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_definitions(reboot_host_kernel PRIVATE _GNU_SOURCE)

add_library(reboot_test_logic STATIC ${CRUX_REBOOT_SOURCES})
target_include_directories(reboot_test_logic PUBLIC ../../components/reboot/src)
target_compile_definitions(reboot_test_logic PUBLIC CRUX_REBOOT_TESTING=1
    CONFIG_CRUX_REBOOT_WORKER_PRIORITY=6 CONFIG_CRUX_REBOOT_WORKER_STACK_SIZE=4096)
target_link_libraries(reboot_test_logic PUBLIC crux_reboot_api crux_access_test_composition
    reboot_host_kernel crux::sync crux_configuration_text Threads::Threads)
target_compile_options(reboot_test_logic PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Fault injection by the lock of the service: see fault_bridge.c. A suite that wraps crux_mutex_lock also
# wraps z_impl_k_mutex_lock and links this object.
add_library(reboot_fault_bridge OBJECT fault_bridge.c)
target_link_libraries(reboot_fault_bridge PRIVATE reboot_host_kernel crux::sync)
target_compile_options(reboot_fault_bridge PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(reboot_test_fixture OBJECT reboot_test.c fake_port.c)
target_include_directories(reboot_test_fixture PUBLIC .)
target_link_libraries(reboot_test_fixture PUBLIC reboot_test_logic)
target_compile_options(reboot_test_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)

foreach(suite IN ITEMS service steps concurrency reason lock_faults)
    add_executable(reboot_${suite} ${suite}.c $<TARGET_OBJECTS:reboot_test_fixture>)
    target_link_libraries(reboot_${suite} PRIVATE reboot_test_fixture)
    target_compile_options(reboot_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(reboot_${suite} PRIVATE _GNU_SOURCE)
    add_test(NAME Reboot.${suite} COMMAND reboot_${suite})
    set_tests_properties(Reboot.${suite} PROPERTIES TIMEOUT 240)
endforeach()

target_sources(reboot_lock_faults PRIVATE $<TARGET_OBJECTS:reboot_fault_bridge>)
target_link_options(reboot_lock_faults PRIVATE -Wl,--wrap=crux_mutex_lock -Wl,--wrap=z_impl_k_mutex_lock)

# The sleep port is k_msleep() now and no Host build compiles it (the fake port stands in for it). The old
# suite of the port, sleep_provider.c, wraps crux_semaphore_take() to give the take's answers and has no
# take to wrap any more; it is kept in this directory and built nowhere. On native_sim the port is held to
# its own checks, in tests/reboot/zephyr.

# Every public declaration must also compile for a C++ consumer.
add_library(reboot_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(reboot_cpp_headers PRIVATE crux_reboot_api)
target_compile_options(reboot_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)

# What a bounded wait for pending saves means, on the real network settings and configuration saves. The
# suite takes the network settings fixture, which boots Access with its own accounts, so it links the
# reboot service and the fake ports but not this directory's fixture.
if(TARGET network_settings_test_fixture AND TARGET crux_web_adapter)
    add_executable(reboot_pending pending.c fake_port.c
        ${CRUX_WEB_MANAGEMENT_DIR}/web_management_save.c
        $<TARGET_OBJECTS:network_settings_test_fixture>)
    target_include_directories(reboot_pending PRIVATE . ${CRUX_WEB_MANAGEMENT_DIR})
    target_link_libraries(reboot_pending PRIVATE reboot_test_logic network_settings_test_fixture
        crux_configuration_fake_provider crux_web_adapter crux_web_api crux_mgmt_status_api)
    target_compile_options(reboot_pending PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(reboot_pending PRIVATE _GNU_SOURCE)
    target_sources(reboot_pending PRIVATE $<TARGET_OBJECTS:reboot_fault_bridge>)
    target_link_options(reboot_pending PRIVATE -Wl,--wrap=crux_mutex_lock -Wl,--wrap=z_impl_k_mutex_lock)
    add_test(NAME Reboot.pending COMMAND reboot_pending)
    set_tests_properties(Reboot.pending PROPERTIES TIMEOUT 240)
endif()

# A composition must not be able to ignore ERR_NO_POLICY. The first object uses the result and builds with
# everything else. The second throws it away, is left out of the normal build, and Reboot.nodiscard builds
# it with -Werror=unused-result and expects that to fail, with the attribute named in the output.
add_library(reboot_nodiscard_ok OBJECT nodiscard_ok.c)
target_link_libraries(reboot_nodiscard_ok PRIVATE crux_reboot_api)
target_compile_options(reboot_nodiscard_ok PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_library(reboot_nodiscard_probe OBJECT EXCLUDE_FROM_ALL probe/nodiscard_probe.c)
target_link_libraries(reboot_nodiscard_probe PRIVATE crux_reboot_api)
target_compile_options(reboot_nodiscard_probe PRIVATE -Werror=unused-result)
add_test(NAME Reboot.nodiscard
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target reboot_nodiscard_probe)
# The build must fail for this reason and no other: a header that does not compile, or a typo in the probe,
# would fail it too, and the output must name the attribute.
set_tests_properties(Reboot.nodiscard PROPERTIES PASS_REGULAR_EXPRESSION "warn_unused_result"
    TIMEOUT 120)
