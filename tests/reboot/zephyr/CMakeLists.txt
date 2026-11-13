# The reboot suites of tests/reboot, compiled unchanged, on native_sim. Stage A ran them over the Crux
# Thread and Sync Zephyr providers; Stage B (ADR-0002 P9) runs them over the Zephyr-direct service
# (components/reboot/ports/zephyr: k_mutex, k_sem, k_thread, k_msleep). No assertion of a suite is
# changed. What differs from the Host is how a case is isolated, since Zephyr has no fork():
# zephyr_case.c runs one case for each process, and scripts/test-reboot-native.sh starts the image once
# for each case.
#
# One image for each wrapping set. REBOOT_IMAGE selects it:
#   core            service, steps, concurrency and reason, as on the Host
#   lock_faults     the suite whose link wraps crux_mutex_lock; fault_bridge.c carries the wrap to the
#                   kernel's lock, which the service takes now
#   sleep_provider  the Zephyr-direct sleep port and the worker's priority, checked by ztest cases of
#                   their own (main_sleep_provider.c). The old suite of the Crux sleep provider has no
#                   take to wrap and runs nowhere
#   pending         the suite that takes the network settings and configuration saves as wait
#                   sources, with crux_mutex_lock wrapped
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_reboot_native LANGUAGES C)

set(REBOOT_IMAGE "core" CACHE STRING "Which suites this image holds: core, lock_faults, sleep_provider or pending")
set(repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(reboot_tests "${repo}/tests/reboot")
set(access_tests "${repo}/tests/access")

# A suite's main() is the Host entry. Here it is a function that the ztest case calls.
function(reboot_suite_source file name)
    set_source_files_properties("${file}" PROPERTIES COMPILE_DEFINITIONS "main=reboot_${name}_main")
endfunction()

target_sources(app PRIVATE main_${REBOOT_IMAGE}.c)
# compat/ holds the pthread.h that the suites find first; see there for why.
target_include_directories(app BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR}/compat)
target_include_directories(app PRIVATE ${reboot_tests} ${access_tests}
    ${repo}/components/reboot/src ${repo}/components/reboot/include
    ${repo}/components/access/src ${repo}/components/access/include ${repo}/components/access/adapter
    ${repo}/components/configuration/include)
# The placement is for the network settings worker of the pending image. The reboot worker needs none.
target_link_libraries(app PRIVATE crux::thread crux::sync crux_service_priority_placement)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

if(REBOOT_IMAGE STREQUAL "core" OR REBOOT_IMAGE STREQUAL "lock_faults" OR REBOOT_IMAGE STREQUAL "pending")
    # The service and the Access core, as the Host test composition builds them.
    target_sources(app PRIVATE
        ${repo}/components/reboot/ports/zephyr/reboot_zephyr.c
        ${repo}/components/configuration/src/configuration_text.c
        ${repo}/components/access/src/access.c
        ${repo}/components/access/src/access_accounts.c
        ${repo}/components/access/src/access_delay.c
        ${repo}/components/access/src/access_login.c
        ${repo}/components/access/src/access_record.c
        ${repo}/components/access/src/access_session.c
        ${access_tests}/fake_ports.c ${access_tests}/access_test.c
        ${reboot_tests}/fake_port.c)
    target_sources(app PRIVATE zephyr_case.c compat_pthread.c)
    target_compile_definitions(app PRIVATE CRUX_ACCESS_TESTING=1 CRUX_ACCESS_TEST_COMPOSITION=1
        CRUX_REBOOT_TESTING=1)
    # reboot_test.c forks each case on the Host. Its two entry points are renamed away and
    # zephyr_case.c supplies the Zephyr ones.
    if(NOT REBOOT_IMAGE STREQUAL "pending")
        # The pending suite boots Access with the network settings fixture and links no reboot_test.c.
        target_sources(app PRIVATE ${reboot_tests}/reboot_test.c)
        set_source_files_properties(${reboot_tests}/reboot_test.c PROPERTIES COMPILE_DEFINITIONS
            "reboot_run_case=reboot_host_run_case;reboot_finish=reboot_host_finish")
    endif()
endif()

if(REBOOT_IMAGE STREQUAL "core")
    foreach(suite IN ITEMS service steps concurrency reason)
        target_sources(app PRIVATE ${reboot_tests}/${suite}.c)
        reboot_suite_source(${reboot_tests}/${suite}.c ${suite})
        set_source_files_properties(${reboot_tests}/${suite}.c PROPERTIES COMPILE_DEFINITIONS
            "main=reboot_${suite}_main;_GNU_SOURCE")
    endforeach()
elseif(REBOOT_IMAGE STREQUAL "lock_faults")
    target_sources(app PRIVATE ${reboot_tests}/lock_faults.c)
    set_source_files_properties(${reboot_tests}/lock_faults.c PROPERTIES COMPILE_DEFINITIONS
        "main=reboot_lock_faults_main;_GNU_SOURCE")
    # app is a static library: the wrap has to reach the final link.
    target_sources(app PRIVATE ${reboot_tests}/fault_bridge.c)
    target_link_options(app PUBLIC -Wl,--wrap=crux_mutex_lock -Wl,--wrap=z_impl_k_mutex_lock)
elseif(REBOOT_IMAGE STREQUAL "pending")
    set(net_tests "${repo}/tests/network_settings")
    target_sources(app PRIVATE
        ${repo}/components/network_settings/src/network_settings.c
        ${repo}/components/network_settings/src/network_settings_check.c
        ${repo}/components/config_service/src/config_service.c
        ${repo}/components/configuration/src/configuration.c
        ${repo}/components/web/management/web_management_save.c
        ${repo}/tests/config_service/fake_events.c ${repo}/tests/configuration/fault_provider.c
        ${net_tests}/net_test.c ${net_tests}/fake_port.c ${reboot_tests}/pending.c)
    target_include_directories(app PRIVATE ${net_tests} ${repo}/tests/configuration ${repo}/tests/config_service
        ${repo}/components/network_settings/src ${repo}/components/network_settings/include
        ${repo}/components/config_service/src ${repo}/components/config_service/include
        ${repo}/components/configuration/src
        ${repo}/components/web/include ${repo}/components/web/adapter ${repo}/components/web/management
        ${repo}/components/mgmt_status/include ${repo}/components/system/include)
    # As the Host fixture builds them: a save waits 50 ms for the service lock, and the network settings
    # service waits 400 ms for a busy service.
    target_compile_definitions(app PRIVATE CRUX_NETWORK_SETTINGS_TESTING=1 CRUX_CONFIG_SERVICE_SAVE_WAIT_MS=50u
        CRUX_NETWORK_SETTINGS_BUSY_WAIT_MS=400u CRUX_NETWORK_SETTINGS_BUSY_STEP_MS=10u CRUX_WEB_BODY_BUFFER=1024u)
    set_source_files_properties(${net_tests}/net_test.c PROPERTIES COMPILE_DEFINITIONS
        "net_run_case=net_host_run_case;net_finish=net_host_finish")
    set_source_files_properties(${reboot_tests}/pending.c PROPERTIES COMPILE_DEFINITIONS
        "main=reboot_pending_main;_GNU_SOURCE")
    target_sources(app PRIVATE ${reboot_tests}/fault_bridge.c)
    target_link_options(app PUBLIC -Wl,--wrap=crux_mutex_lock -Wl,--wrap=z_impl_k_mutex_lock)
elseif(REBOOT_IMAGE STREQUAL "sleep_provider")
    # The service over its real sleep port, and the three ports and the permission check that it calls,
    # which main_sleep_provider.c supplies.
    target_sources(app PRIVATE ${repo}/components/reboot/ports/zephyr/reboot_zephyr.c
        ${repo}/components/reboot/ports/zephyr/reboot_sleep_zephyr.c
        ${repo}/components/configuration/src/configuration_text.c)
    target_compile_definitions(app PRIVATE CRUX_REBOOT_TESTING=1)
else()
    message(FATAL_ERROR "unknown REBOOT_IMAGE ${REBOOT_IMAGE}")
endif()
