# The instrumented core and the fake ports form a test composition. They exist only when Crux builds
# its own tests, and no public target or provider setting can select them. Each composition has its
# own slot size, because that is a build setting. It leaves out the File provider, which each suite
# picks.
function(credential_test_composition name slot_bytes)
    add_library(${name} STATIC ${CRUX_CREDENTIAL_SOURCES} fake_ports.c credential_test.c)
    target_include_directories(${name} PUBLIC . ../../components/credential/src)
    target_link_libraries(${name} PUBLIC crux_credential_adapter crux::sync Threads::Threads)
    target_compile_definitions(${name} PUBLIC
        CRUX_CREDENTIAL_TESTING=1 CRUX_CREDENTIAL_TEST_COMPOSITION=1
        CRUX_CREDENTIAL_SLOT_BYTES=${slot_bytes})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

credential_test_composition(crux_credential_test_composition 16384)
credential_test_composition(crux_credential_test_composition_small 1024)

function(credential_suite name timeout)
    add_executable(${name} ${ARGN})
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME Credential.${name} COMMAND ${name})
    set_tests_properties(Credential.${name} PROPERTIES TIMEOUT ${timeout})
endfunction()

# The same contract on each File provider.
credential_suite(credential_conformance_fake 60 conformance_fake.c conformance.c medium_fake.c)
target_link_libraries(credential_conformance_fake PRIVATE crux_credential_test_composition
    crux_file_fake)
credential_suite(credential_conformance_linux 60 conformance_linux.c conformance.c medium_linux.c)
target_link_libraries(credential_conformance_linux PRIVATE crux_credential_test_composition
    crux::file)
target_include_directories(credential_conformance_linux PRIVATE ../../components/file/ports/linux)
target_compile_definitions(credential_conformance_linux PRIVATE _DEFAULT_SOURCE)

credential_suite(credential_concurrency 120 concurrency.c medium_linux.c)
target_link_libraries(credential_concurrency PRIVATE crux_credential_test_composition crux::file)
target_include_directories(credential_concurrency PRIVATE ../../components/file/ports/linux)
target_compile_definitions(credential_concurrency PRIVATE _DEFAULT_SOURCE)

# Power loss and failures, which need the fake. The small slot makes a walk of every byte quick and
# reaches the slot's limits, and the default slot makes a walk of its sectors real.
credential_suite(credential_faults 300 faults.c medium_fake.c)
target_link_libraries(credential_faults PRIVATE crux_credential_test_composition_small
    crux_file_fake)
credential_suite(credential_faults_default_slot 120 faults.c medium_fake.c)
target_link_libraries(credential_faults_default_slot PRIVATE crux_credential_test_composition
    crux_file_fake)
foreach(suite IN ITEMS credential_faults credential_faults_default_slot)
    target_link_options(${suite} PRIVATE -Wl,--wrap=crux_mutex_lock -Wl,--wrap=crux_mutex_create)
endforeach()

credential_suite(credential_security 60 security.c medium_fake.c)
target_link_libraries(credential_security PRIVATE crux_credential_test_composition crux_file_fake)

# A product build of the core, linked with the test-only ports by mistake, must refuse them.
credential_suite(credential_production_refuses 20 production_refuses.c fake_ports.c medium_fake.c)
target_link_libraries(credential_production_refuses PRIVATE crux_credential_core
    crux_credential_adapter crux_file_fake)
target_include_directories(credential_production_refuses PRIVATE .
    ../../components/credential/src)
target_compile_definitions(credential_production_refuses PRIVATE
    CRUX_CREDENTIAL_TEST_COMPOSITION=1)

# The real Access core on the real store, through the adapter a composition links.
if(CRUX_BUILD_ACCESS)
    add_library(credential_access_composition STATIC ${CRUX_ACCESS_SOURCES} access_ports.c)
    target_include_directories(credential_access_composition PUBLIC ../../components/access/src)
    target_link_libraries(credential_access_composition PUBLIC crux_access_adapter crux::sync
        Threads::Threads)
    target_compile_definitions(credential_access_composition PUBLIC CRUX_ACCESS_TESTING=1)
    target_compile_options(credential_access_composition PRIVATE -Wall -Wextra -Wpedantic -Werror)

    credential_suite(credential_access_store 120 access_store.c medium_fake.c)
    target_link_libraries(credential_access_store PRIVATE crux_credential_access_store
        credential_access_composition crux_credential_test_composition crux_file_fake)
endif()

# Every public and adapter declaration must also compile for a C++ consumer.
add_library(credential_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(credential_cpp_headers PRIVATE crux_credential_adapter)
target_compile_options(credential_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)
