# The instrumented core with controlled Time, Logging and Sync and Thread creators, Access's fake
# ports and the fake of this contract are test compositions. They exist only when Crux builds its own
# tests, and no public target or provider setting selects them.
set(_security_event_wraps
    crux_monotonic_now crux_realtime_get_snapshot crux_log_write crux_mutex_create
    crux_semaphore_create crux_semaphore_give crux_semaphore_take crux_thread_create
    crux_thread_sleep_for crux_is_in_isr)
list(TRANSFORM _security_event_wraps PREPEND "-Wl,--wrap=" OUTPUT_VARIABLE _security_event_wrap_options)
set(_security_event_warnings -Wall -Wextra -Wpedantic -Werror -UNDEBUG)

function(security_event_test_core name drain_wait_ms)
    add_library(${name} STATIC ${CRUX_SECURITY_EVENT_SOURCES} event_test.c)
    target_include_directories(${name}
        PUBLIC . ../../components/security_event/src
        PRIVATE ../../components/interrupt/src)
    target_link_libraries(${name}
        PUBLIC crux_security_event_api crux::logging crux::sync crux::thread crux::time
        PRIVATE crux_interrupt)
    # Real threads in these suites need no real pacing, which the integration suite covers.
    target_compile_definitions(${name} PUBLIC
        CRUX_SECURITY_EVENT_TESTING=1 CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS=1
        CRUX_SECURITY_EVENT_DRAIN_WAIT_MS=${drain_wait_ms} ${ARGN})
    target_compile_options(${name} PRIVATE ${_security_event_warnings})
endfunction()

function(security_event_test_suite name core)
    add_executable(security_event_${name} ${ARGN})
    target_link_libraries(security_event_${name} PRIVATE ${core} crux_interrupt)
    target_compile_options(security_event_${name} PRIVATE ${_security_event_warnings})
    target_compile_definitions(security_event_${name} PRIVATE _POSIX_C_SOURCE=200809L)
    target_link_options(security_event_${name} PRIVATE ${_security_event_wrap_options})
    add_test(NAME SecurityEvent.${name} COMMAND security_event_${name})
    set_tests_properties(SecurityEvent.${name} PROPERTIES TIMEOUT 120)
endfunction()

security_event_test_core(security_event_test_core 5)
# A different build-time ring, so that a default does not hide in a case.
security_event_test_core(security_event_test_core_small 5
    CRUX_SECURITY_EVENT_RING=8 CRUX_SECURITY_EVENT_RESERVE=2 CRUX_SECURITY_EVENT_DRAIN_BURST=3)
# A drain whose timed wake never fires in a run, so that only the signal can move it.
security_event_test_core(security_event_test_core_signal 60000)

# The core alone, answering the read path's permission check with a stub.
foreach(suite IN ITEMS conformance ring time_cases init concurrency)
    security_event_test_suite(${suite} security_event_test_core ${suite}.c access_check_stub.c)
endforeach()
security_event_test_suite(ring_small security_event_test_core_small ring.c access_check_stub.c)
security_event_test_suite(drain security_event_test_core_signal drain.c access_check_stub.c)
target_link_libraries(security_event_concurrency PRIVATE Threads::Threads)

# The real Access core and the real provider, with Access's fake ports. The fake keeps its recording
# port under another name, because the provider owns crux_access_port_event in this composition.
add_library(security_event_access_composition STATIC ${CRUX_ACCESS_SOURCES} ../access/access_test.c)
target_include_directories(security_event_access_composition
    PUBLIC ../access ../../components/access/src)
target_link_libraries(security_event_access_composition
    PUBLIC crux_access_adapter crux::sync Threads::Threads)
target_compile_definitions(security_event_access_composition
    PUBLIC CRUX_ACCESS_TESTING=1 CRUX_ACCESS_TEST_COMPOSITION=1)
target_compile_options(security_event_access_composition PRIVATE ${_security_event_warnings})

add_library(security_event_access_fakes OBJECT ../access/fake_ports.c)
target_include_directories(security_event_access_fakes
    PRIVATE ../access ../../components/access/src)
target_link_libraries(security_event_access_fakes PRIVATE crux_access_adapter)
target_compile_definitions(security_event_access_fakes PRIVATE
    CRUX_ACCESS_TEST_COMPOSITION=1 crux_access_port_event=access_fake_port_event)
target_compile_options(security_event_access_fakes PRIVATE ${_security_event_warnings})

function(security_event_joined_suite name)
    security_event_test_suite(${name} security_event_test_core ${name}.c joined.c ${ARGN})
    target_link_libraries(security_event_${name} PRIVATE
        crux_security_event_access_port security_event_access_fakes
        security_event_access_composition)
    target_link_options(security_event_${name} PRIVATE -Wl,--wrap=crux_access_port_event)
endfunction()

security_event_joined_suite(rejections)
security_event_joined_suite(read)
target_link_options(security_event_read PRIVATE -Wl,--wrap=crux_access_check)
security_event_joined_suite(waits)
# Access's own secret suite, run through the real provider and core. Its checks stay as they are.
security_event_joined_suite(secrets ../access/secrets.c)
set_source_files_properties(../access/secrets.c PROPERTIES COMPILE_DEFINITIONS main=access_secrets_main)

# What a composition links: product cores with no test hooks, the provider as objects, and real
# Logging, Time, Thread and Sync. Only Access's crypto and storage ports are fakes.
add_executable(security_event_integration integration.c)
target_include_directories(security_event_integration
    PRIVATE ../access ../../components/access/src ../../components/time/src)
target_link_libraries(security_event_integration PRIVATE
    crux::security_event crux_security_event_access_port security_event_access_fakes
    crux_access_adapter crux::logging crux::thread crux::sync crux::time Threads::Threads)
target_compile_definitions(security_event_integration PRIVATE
    CRUX_ACCESS_TEST_COMPOSITION=1 _POSIX_C_SOURCE=200809L)
target_compile_options(security_event_integration PRIVATE ${_security_event_warnings})
add_test(NAME SecurityEvent.integration COMMAND security_event_integration)
set_tests_properties(SecurityEvent.integration PROPERTIES TIMEOUT 120)

# A recording fake of the public contract, for the Host tests of a consumer such as the status service
# or the command line. It passes the same public cases as the core.
add_library(crux_security_event_fake STATIC
    fake/security_event_fake.c ../../components/security_event/src/security_event_fields.c)
target_include_directories(crux_security_event_fake
    PUBLIC fake
    PRIVATE ../../components/security_event/src)
target_link_libraries(crux_security_event_fake
    PUBLIC crux_security_event_api
    PRIVATE crux_logging_api crux_sync_api Threads::Threads)
target_compile_definitions(crux_security_event_fake PRIVATE CRUX_SECURITY_EVENT_TEST_COMPOSITION=1)
target_compile_options(crux_security_event_fake PRIVATE ${_security_event_warnings})

security_event_test_suite(public_core security_event_test_core public_cases.c public_driver_core.c
    access_check_stub.c)

foreach(suite IN ITEMS public_fake fake_cases)
    set(_sources public_cases.c public_driver_fake.c)
    if(suite STREQUAL "fake_cases")
        set(_sources fake_cases.c)
    endif()
    add_executable(security_event_${suite} ${_sources})
    target_link_libraries(security_event_${suite} PRIVATE crux_security_event_fake)
    target_compile_options(security_event_${suite} PRIVATE ${_security_event_warnings})
    add_test(NAME SecurityEvent.${suite} COMMAND security_event_${suite})
    set_tests_properties(SecurityEvent.${suite} PROPERTIES TIMEOUT 30)
endforeach()

# Every public declaration must also compile for a C++ consumer.
add_library(security_event_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(security_event_cpp_headers PRIVATE crux_security_event_api)
target_compile_options(security_event_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)
