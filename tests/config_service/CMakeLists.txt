# The recording event port, which an adapter's tests can link too. It exists only when Crux builds
# its own tests, and no public target or provider setting can select it.
add_library(crux_config_service_fake_events OBJECT fake_events.c)
target_include_directories(crux_config_service_fake_events
    PUBLIC . ../../components/config_service/src)
target_link_libraries(crux_config_service_fake_events PUBLIC crux_config_service_api
    PRIVATE Threads::Threads)
target_compile_options(crux_config_service_fake_events PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The fake Configuration store, which an adapter's tests can link too.
add_library(crux_configuration_fake_provider OBJECT ../configuration/fault_provider.c)
target_include_directories(crux_configuration_fake_provider
    PUBLIC ../configuration ../../components/configuration/src)
target_link_libraries(crux_configuration_fake_provider PUBLIC crux::configuration)
target_compile_options(crux_configuration_fake_provider PRIVATE -Wall -Wextra -Wpedantic -Werror)

set(CRUX_CONFIG_SERVICE_SOURCE ../../components/config_service/src/config_service.c)

# Each suite compiles the service itself, so that a build can lower the wait of a save.
function(crux_config_service_suite suite)
    add_executable(config_service_${suite} ${suite}.c service_test.c ${CRUX_CONFIG_SERVICE_SOURCE})
    target_link_libraries(config_service_${suite} PRIVATE crux_config_service_api
        crux_access_test_composition crux_config_service_fake_events
        crux_configuration_fake_provider crux::sync Threads::Threads)
    target_compile_definitions(config_service_${suite} PRIVATE _POSIX_C_SOURCE=200809L
        CRUX_CONFIG_SERVICE_SAVE_WAIT_MS=50u)
    target_compile_options(config_service_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME ConfigService.${suite} COMMAND config_service_${suite})
    set_tests_properties(ConfigService.${suite} PROPERTIES TIMEOUT 60)
endfunction()

foreach(suite IN ITEMS permissions values save concurrency)
    crux_config_service_suite(${suite})
endforeach()

add_executable(config_service_lock_faults lock_faults.c access_double.c ${CRUX_CONFIG_SERVICE_SOURCE})
target_link_libraries(config_service_lock_faults PRIVATE crux_config_service_api
    crux_config_service_fake_events crux_configuration_fake_provider crux::sync)
target_link_options(config_service_lock_faults PRIVATE -Wl,--wrap=crux_mutex_lock
    -Wl,--wrap=crux_mutex_create)
target_compile_options(config_service_lock_faults PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME ConfigService.lock_faults COMMAND config_service_lock_faults)
set_tests_properties(ConfigService.lock_faults PROPERTIES TIMEOUT 60)

# Over the no-provider port, as a composition selects it, with no fake store linked.
add_executable(config_service_no_provider no_provider.c access_double.c ${CRUX_CONFIG_SERVICE_SOURCE})
target_link_libraries(config_service_no_provider PRIVATE crux_config_service_api
    crux_config_service_fake_events crux_configuration_no_provider crux::sync)
target_compile_options(config_service_no_provider PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME ConfigService.no_provider COMMAND config_service_no_provider)
set_tests_properties(ConfigService.no_provider PROPERTIES TIMEOUT 60)

# Every public declaration must also compile for a C++ consumer.
add_library(config_service_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(config_service_cpp_headers PRIVATE crux_config_service_api)
target_compile_options(config_service_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)
