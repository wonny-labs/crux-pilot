# Stage A of the reboot pilot (ADR-0002 P9): the existing reboot suites of tests/reboot, compiled
# unchanged, on native_sim over the Crux Thread and Sync Zephyr providers. The sources of the service
# are not touched and no assertion of a suite is changed. What differs from the Host is how a case is
# isolated, since Zephyr has no fork(): zephyr_case.c runs one case for each process, and
# scripts/test-reboot-native.sh starts the image once for each case.
#
# One image for each wrapping set. REBOOT_IMAGE selects it:
#   core            service, steps, concurrency and reason, as on the Host
#   lock_faults     the suite whose link wraps crux_mutex_lock
#   sleep_provider  the sleep provider, whose link wraps crux_semaphore_take
#   pending         the suite that takes the network settings and configuration saves as wait
#                   sources, with crux_mutex_lock wrapped
#   web             the two GET and POST /api/v1/system/reboot suites of tests/web/reboot, on the
#                   real Web core and session module, with crux_web_respond and crux_mutex_lock wrapped
#   web_no_service  the suite whose route table has no reboot route
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_reboot_native LANGUAGES C)

set(REBOOT_IMAGE "core" CACHE STRING "Which suites this image holds: core, lock_faults, sleep_provider, pending, web or web_no_service")
set(repo "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(reboot_tests "${repo}/tests/reboot")
set(access_tests "${repo}/tests/access")

target_sources(app PRIVATE main_${REBOOT_IMAGE}.c)
# compat/ holds the pthread.h that the suites find first; see there for why.
target_include_directories(app BEFORE PRIVATE ${CMAKE_CURRENT_LIST_DIR}/compat)
target_include_directories(app PRIVATE ${reboot_tests} ${access_tests}
    ${repo}/components/reboot/src ${repo}/components/reboot/include
    ${repo}/components/access/src ${repo}/components/access/include ${repo}/components/access/adapter
    ${repo}/components/configuration/include)
target_link_libraries(app PRIVATE crux::thread crux::sync crux_service_priority_placement)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

if(REBOOT_IMAGE MATCHES "^(core|lock_faults|pending|web|web_no_service)$")
    # The service and the Access core, as the Host test composition builds them.
    target_sources(app PRIVATE
        ${repo}/components/reboot/src/reboot.c
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
    target_compile_definitions(app PRIVATE CRUX_ACCESS_TESTING=1 CRUX_ACCESS_TEST_COMPOSITION=1)
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
        set_source_files_properties(${reboot_tests}/${suite}.c PROPERTIES COMPILE_DEFINITIONS
            "main=reboot_${suite}_main;_GNU_SOURCE")
    endforeach()
elseif(REBOOT_IMAGE STREQUAL "lock_faults")
    target_sources(app PRIVATE ${reboot_tests}/lock_faults.c)
    set_source_files_properties(${reboot_tests}/lock_faults.c PROPERTIES COMPILE_DEFINITIONS
        "main=reboot_lock_faults_main;_GNU_SOURCE")
    # app is a static library: the wrap has to reach the final link.
    target_link_options(app PUBLIC -Wl,--wrap=crux_mutex_lock)
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
    target_link_options(app PUBLIC -Wl,--wrap=crux_mutex_lock)
elseif(REBOOT_IMAGE STREQUAL "web" OR REBOOT_IMAGE STREQUAL "web_no_service")
    # The routes on the real Web core and session module, over the fake transport and fake session ports
    # of tests/web, as crux_web_reboot_core() builds them for the Host.
    set(web "${repo}/components/web")
    set(web_tests "${repo}/tests/web")
    include(${repo}/cmake/CruxWebRoutes.cmake)
    target_sources(app PRIVATE
        ${web}/src/web_core.c ${web}/src/web_response.c ${web}/src/web_route.c
        ${web}/session/web_session.c ${web}/session/web_session_attempt.c ${web}/session/web_session_json.c
        ${web_tests}/fake_transport.c ${web_tests}/session/session_test.c ${web_tests}/session/test_routes.c
        ${web_tests}/reboot/reboot_web.c)
    target_include_directories(app PRIVATE ${web_tests} ${web_tests}/session ${web_tests}/reboot
        ${web}/src ${web}/include ${web}/adapter ${web}/session ${web}/management
        ${repo}/components/configuration/src ${repo}/components/system/include)
    target_compile_definitions(app PRIVATE CRUX_WEB_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3 CRUX_WEB_SESSION_MAX=10
        CRUX_WEB_BODY_BUFFER=1024u _GNU_SOURCE)
    # session_test.c waits with the host's nanosleep(), which would block the whole simulation.
    set_source_files_properties(${web_tests}/session/session_test.c PROPERTIES COMPILE_OPTIONS
        "-include;${CMAKE_CURRENT_LIST_DIR}/compat/pthread.h")
    set(routes ${web}/session/web_session_routes.c)
    if(REBOOT_IMAGE STREQUAL "web")
        list(APPEND routes ${web}/management/web_management_reboot.c)
        target_sources(app PRIVATE ${web}/management/web_management_reboot.c)
        foreach(suite IN ITEMS routes openapi_fixture)
            target_sources(app PRIVATE ${web_tests}/reboot/${suite}.c)
            set_source_files_properties(${web_tests}/reboot/${suite}.c PROPERTIES COMPILE_DEFINITIONS
                "main=reboot_${suite}_main;_GNU_SOURCE")
        endforeach()
    else()
        target_sources(app PRIVATE ${web_tests}/reboot/no_service.c)
        set_source_files_properties(${web_tests}/reboot/no_service.c PROPERTIES COMPILE_DEFINITIONS
            "main=reboot_no_service_main;_GNU_SOURCE")
    endif()
    target_sources(app PRIVATE ${web}/session/web_session_routes.c)
    crux_web_routes(app SOURCES ${routes} ${web_tests}/session/test_routes.c
        FAKE_MANIFEST REVIEWED ${web_tests}/reboot/reviewed-routes.txt)
    target_link_options(app PUBLIC -Wl,--wrap=crux_web_respond -Wl,--wrap=crux_mutex_lock)
elseif(REBOOT_IMAGE STREQUAL "sleep_provider")
    target_sources(app PRIVATE ${reboot_tests}/sleep_provider.c ${repo}/components/reboot/src/reboot_sleep.c)
    set_source_files_properties(${reboot_tests}/sleep_provider.c PROPERTIES COMPILE_DEFINITIONS
        "main=reboot_sleep_provider_main")
    target_link_options(app PUBLIC -Wl,--wrap=crux_semaphore_take)
else()
    message(FATAL_ERROR "unknown REBOOT_IMAGE ${REBOOT_IMAGE}")
endif()
