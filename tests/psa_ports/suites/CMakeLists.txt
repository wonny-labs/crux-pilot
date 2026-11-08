# The existing Access and credential suites with their crypto on the real PSA providers, on TF-PSA-Crypto.
# The fakes keep their counters, hooks and fault injection, and the shims run the real provider after them.
# Access's faults suite stays out: it needs a derive quicker than a change to the account, which real PBKDF2
# is not, so its concurrent logins are all refused. Random stays the fake's in the Access suites, because some
# place names in delay buckets by it. tests/psa_ports runs the real random port.
# Only -DCRUX_TEST_SUITES_ON_PSA=ON builds this, so the root build still holds no PSA.
include("${PROJECT_SOURCE_DIR}/cmake/CruxPsaCrypto.cmake")
crux_psa_tf_psa_crypto_add()

add_library(crux_access_psa_renamed OBJECT ../../../components/access/ports/psa/access_psa.c)
target_include_directories(crux_access_psa_renamed PRIVATE ../../../components/access/src)
target_link_libraries(crux_access_psa_renamed PRIVATE crux_access_adapter tfpsacrypto)
target_compile_definitions(crux_access_psa_renamed PRIVATE
    crux_access_port_derive=psa_access_port_derive crux_access_port_random=psa_access_port_random
    crux_access_port_hmac=psa_access_port_hmac)

add_library(crux_credential_psa_renamed OBJECT ../../../components/credential/ports/psa/credential_psa.c)
target_include_directories(crux_credential_psa_renamed PRIVATE
    ../../../components/credential/src ../../../components/credential/ports/psa)
target_link_libraries(crux_credential_psa_renamed PRIVATE crux_credential_adapter tfpsacrypto)
target_compile_definitions(crux_credential_psa_renamed PRIVATE
    crux_credential_port_declaration=psa_credential_port_declaration
    crux_credential_port_seal=psa_credential_port_seal
    crux_credential_port_open=psa_credential_port_open
    crux_credential_port_generate_key=psa_credential_port_generate_key
    crux_credential_port_random=psa_credential_port_random)

foreach(suite IN ITEMS conformance accounts security keys secrets events)
    add_executable(psa_access_${suite} ../../access/${suite}.c access_shim.c)
    target_link_libraries(psa_access_${suite} PRIVATE crux_access_test_composition
        crux_access_psa_renamed tfpsacrypto)
    target_compile_definitions(psa_access_${suite} PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_options(psa_access_${suite} PRIVATE -UNDEBUG)
    target_link_options(psa_access_${suite} PRIVATE -Wl,--wrap=crux_access_port_derive
        -Wl,--wrap=crux_access_port_hmac
        -Wl,--wrap=access_fake_derive_force -Wl,--wrap=access_fake_hmac_fixed)
    add_test(NAME RealPorts.access_${suite} COMMAND psa_access_${suite})
    set_tests_properties(RealPorts.access_${suite} PROPERTIES TIMEOUT 900)
endforeach()

function(psa_credential_suite name)
    add_executable(psa_${name} ${ARGN} credential_shim.c)
    target_include_directories(psa_${name} PRIVATE ../../credential ../../../components/credential/ports/psa)
    target_link_libraries(psa_${name} PRIVATE crux_credential_test_composition
        crux_credential_psa_renamed tfpsacrypto)
    target_compile_options(psa_${name} PRIVATE -UNDEBUG)
    target_link_options(psa_${name} PRIVATE -Wl,--wrap=crux_credential_port_seal
        -Wl,--wrap=crux_credential_port_open -Wl,--wrap=crux_credential_port_generate_key
        -Wl,--wrap=crux_credential_port_random -Wl,--wrap=credential_fake_open_fail
        -Wl,--wrap=credential_fake_device_key -Wl,--wrap=credential_fake_reset)
    add_test(NAME RealPorts.${name} COMMAND psa_${name})
    set_tests_properties(RealPorts.${name} PROPERTIES TIMEOUT 900)
endfunction()
psa_credential_suite(credential_conformance_fake ../../credential/conformance_fake.c
    ../../credential/conformance.c ../../credential/medium_fake.c)
target_link_libraries(psa_credential_conformance_fake PRIVATE crux_file_fake)
psa_credential_suite(credential_security ../../credential/security.c ../../credential/medium_fake.c)
target_link_libraries(psa_credential_security PRIVATE crux_file_fake)
psa_credential_suite(credential_faults_default_slot ../../credential/faults.c
    ../../credential/medium_fake.c)
target_link_libraries(psa_credential_faults_default_slot PRIVATE crux_file_fake)
target_link_options(psa_credential_faults_default_slot PRIVATE
    -Wl,--wrap=crux_mutex_lock -Wl,--wrap=crux_mutex_create)
psa_credential_suite(credential_concurrency ../../credential/concurrency.c
    ../../credential/medium_linux.c)
target_compile_definitions(psa_credential_concurrency PRIVATE _DEFAULT_SOURCE)
target_include_directories(psa_credential_concurrency PRIVATE ../../../components/file/ports/linux)
target_link_libraries(psa_credential_concurrency PRIVATE crux::file)
