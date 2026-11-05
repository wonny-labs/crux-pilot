# The thin providers that a management composition links, each against a controlled answer. The real
# services and real crypto meet them in tests/native/management_composition. These exist only when
# Crux builds its own tests.
foreach(required IN ITEMS crux_config_service_security_events crux_mgmt_status_security_events
        crux_mgmt_status_credential crux_security_event_credential_port crux_security_event_fake
        crux_access_test_composition)
    if(NOT TARGET ${required})
        return()
    endif()
endforeach()

set(_management_providers_warnings -Wall -Wextra -Wpedantic -Werror -UNDEBUG)

add_executable(management_providers_events events.c)
target_include_directories(management_providers_events PRIVATE
    ../../components/config_service/src ../../components/mgmt_status/src ../../components/credential/src)
target_link_libraries(management_providers_events PRIVATE
    crux_config_service_security_events crux_mgmt_status_security_events crux_mgmt_status_credential
    crux_security_event_credential_port crux_security_event_fake crux_access_test_composition
    crux_config_service_api crux_mgmt_status_api crux_credential_adapter Threads::Threads)
target_compile_definitions(management_providers_events PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(management_providers_events PRIVATE ${_management_providers_warnings})
add_test(NAME ManagementProviders.events COMMAND management_providers_events)
set_tests_properties(ManagementProviders.events PROPERTIES TIMEOUT 60)

# A composition can drop either half of what status reads.
add_executable(management_providers_halves halves.c)
target_include_directories(management_providers_halves PRIVATE ../../components/mgmt_status/src)
target_link_libraries(management_providers_halves PRIVATE crux_mgmt_status_no_credential
    crux_mgmt_status_security_events crux_security_event_fake crux_mgmt_status_api
    crux_access_test_composition Threads::Threads)
target_compile_options(management_providers_halves PRIVATE ${_management_providers_warnings})
add_test(NAME ManagementProviders.halves COMMAND management_providers_halves)
set_tests_properties(ManagementProviders.halves PROPERTIES TIMEOUT 60)

if(TARGET crux_access_time_clock)
    add_executable(management_providers_clock clock.c)
    target_include_directories(management_providers_clock PRIVATE ../../components/access/src)
    target_link_libraries(management_providers_clock PRIVATE crux_access_time_clock
        crux_access_adapter crux::base)
    target_compile_options(management_providers_clock PRIVATE ${_management_providers_warnings})
    add_test(NAME ManagementProviders.clock COMMAND management_providers_clock)
    set_tests_properties(ManagementProviders.clock PROPERTIES TIMEOUT 60)
endif()
