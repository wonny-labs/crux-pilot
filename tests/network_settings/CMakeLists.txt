# The network settings service on the real Access core and the real Configuration service, over the
# fake Configuration provider, a fake apply port and a recording event port. All of these exist only
# when Crux builds its own tests. The Configuration service is compiled here, as its own suites do, so
# that a save waits 50 ms for the service lock and not the 5 s of a product, and this service waits
# 400 ms for a busy service, not the seconds of a product.
find_package(Threads REQUIRED)

add_library(network_settings_test_logic STATIC ${CRUX_NETWORK_SETTINGS_SOURCES}
    ../../components/config_service/src/config_service.c)
target_include_directories(network_settings_test_logic PUBLIC
    ../../components/network_settings/src ../../components/config_service/src)
target_compile_definitions(network_settings_test_logic PUBLIC CRUX_NETWORK_SETTINGS_TESTING=1
    CRUX_CONFIG_SERVICE_SAVE_WAIT_MS=50u CRUX_NETWORK_SETTINGS_BUSY_WAIT_MS=400u
    CRUX_NETWORK_SETTINGS_BUSY_STEP_MS=10u)
target_link_libraries(network_settings_test_logic PUBLIC crux_network_settings_api
    crux_access_test_composition crux_config_service_fake_events crux::sync crux::thread
    crux::configuration Threads::Threads)
target_compile_options(network_settings_test_logic PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(network_settings_test_fixture OBJECT net_test.c fake_port.c)
target_include_directories(network_settings_test_fixture PUBLIC . ../configuration)
target_link_libraries(network_settings_test_fixture PUBLIC network_settings_test_logic)
target_compile_options(network_settings_test_fixture PRIVATE -Wall -Wextra -Wpedantic -Werror)

function(crux_network_settings_suite suite)
    add_executable(network_settings_${suite} ${suite}.c $<TARGET_OBJECTS:network_settings_test_fixture>)
    target_link_libraries(network_settings_${suite} PRIVATE network_settings_test_fixture
        crux_configuration_fake_provider)
    target_compile_options(network_settings_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(network_settings_${suite} PRIVATE _GNU_SOURCE)
    add_test(NAME NetworkSettings.${suite} COMMAND network_settings_${suite})
    set_tests_properties(NetworkSettings.${suite} PROPERTIES TIMEOUT 240)
endfunction()

foreach(suite IN ITEMS validation boot service power_loss foreign_save busy concurrency lock_faults)
    crux_network_settings_suite(${suite})
endforeach()
target_compile_definitions(network_settings_validation PRIVATE
    NETWORK_VECTORS_PATH="${PROJECT_SOURCE_DIR}/web/api/network-validation-vectors.tsv")
target_link_options(network_settings_foreign_save PRIVATE
    -Wl,--wrap=crux_config_service_try_update)
target_link_options(network_settings_lock_faults PRIVATE -Wl,--wrap=crux_mutex_lock)

# On the no-provider port, as a composition selects it while no Configuration provider exists.
add_executable(network_settings_no_provider no_provider.c
    $<TARGET_OBJECTS:network_settings_test_fixture>)
target_link_libraries(network_settings_no_provider PRIVATE network_settings_test_fixture
    crux_configuration_no_provider)
target_compile_options(network_settings_no_provider PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME NetworkSettings.no_provider COMMAND network_settings_no_provider)
set_tests_properties(NetworkSettings.no_provider PROPERTIES TIMEOUT 60)

# Every public declaration must also compile for a C++ consumer.
add_library(network_settings_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(network_settings_cpp_headers PRIVATE crux_network_settings_api)
target_compile_options(network_settings_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The recovery command on the real registry, with the scripted access control of the command line suites.
if(TARGET cli_test_scripted)
    add_executable(network_settings_cli cli.c ${CRUX_NETWORK_SETTINGS_SOURCES}
        ../../components/network_settings/src/network_settings_cli.c
        ../../components/config_service/src/config_service.c)
    target_include_directories(network_settings_cli PRIVATE ../../components/network_settings/src
        ../../components/config_service/src ../configuration)
    target_compile_definitions(network_settings_cli PRIVATE CRUX_NETWORK_SETTINGS_TESTING=1
        CRUX_CONFIG_SERVICE_SAVE_WAIT_MS=50u _GNU_SOURCE)
    target_link_libraries(network_settings_cli PRIVATE cli_test_scripted crux_network_settings_api
        crux_config_service_fake_events crux_configuration_fake_provider crux::configuration
        crux::sync crux::thread Threads::Threads)
    target_compile_options(network_settings_cli PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME NetworkSettings.cli COMMAND network_settings_cli)
    set_tests_properties(NetworkSettings.cli PROPERTIES TIMEOUT 60)
endif()
