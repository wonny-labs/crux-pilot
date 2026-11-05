function(tls_certificate_suite name timeout)
    add_executable(${name} ${ARGN} tls_test.c fixtures.c)
    target_include_directories(${name} PRIVATE . ../../components/tls_certificate/src)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(${name} PRIVATE _POSIX_C_SOURCE=200809L)
    add_test(NAME TlsCertificate.${name} COMMAND ${name})
    set_tests_properties(TlsCertificate.${name} PROPERTIES TIMEOUT ${timeout})
endfunction()

# The parser and builders alone, with no state and no crypto.
tls_certificate_suite(tls_certificate_x509 120 x509.c
    ../../components/tls_certificate/src/tls_x509.c)
target_link_libraries(tls_certificate_x509 PRIVATE crux_tls_certificate_api)

# The instrumented core, with the fake ports and credential adapter, on the real Access core and its
# fake ports. Only Crux's own tests build it, and no public target or provider setting selects it.
add_library(tls_certificate_test_composition STATIC ${CRUX_TLS_CERTIFICATE_SOURCES}
    fake_ports.c fake_credential.c fake_credential_scalar.c service_test.c tls_test.c fixtures.c)
target_include_directories(tls_certificate_test_composition PUBLIC .
    ../../components/tls_certificate/src ../../components/tls_certificate/adapter)
target_link_libraries(tls_certificate_test_composition PUBLIC crux_tls_certificate_api
    crux_credential_adapter crux_access_test_composition crux::sync)
target_compile_definitions(tls_certificate_test_composition PUBLIC
    CRUX_TLS_CERTIFICATE_TESTING=1 _POSIX_C_SOURCE=200809L)
target_compile_options(tls_certificate_test_composition PRIVATE -Wall -Wextra -Wpedantic -Werror)

foreach(suite IN ITEMS service install)
    add_executable(tls_certificate_${suite} ${suite}.c)
    target_link_libraries(tls_certificate_${suite} PRIVATE tls_certificate_test_composition)
    target_compile_options(tls_certificate_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME TlsCertificate.${suite} COMMAND tls_certificate_${suite})
    set_tests_properties(TlsCertificate.${suite} PROPERTIES TIMEOUT 120)
endforeach()

# The event provider, on the recording fake of the event contract.
if(TARGET crux_tls_certificate_security_events AND TARGET crux_security_event_fake)
    tls_certificate_suite(tls_certificate_events 30 events.c)
    target_link_libraries(tls_certificate_events PRIVATE crux_tls_certificate_security_events
        crux_tls_certificate_api crux_security_event_fake)
endif()

# The instrumented core on the real credential store, with its test-only protection and the fake
# File provider.
add_executable(tls_certificate_store store.c ${CRUX_TLS_CERTIFICATE_SOURCES} fake_ports.c
    tls_test.c fixtures.c fake_credential_scalar.c ../credential/medium_fake.c)
target_include_directories(tls_certificate_store PRIVATE . ../credential
    ../../components/tls_certificate/src ../../components/tls_certificate/adapter)
target_link_libraries(tls_certificate_store PRIVATE crux_tls_certificate_api
    crux_credential_test_composition crux_file_fake crux_access_test_composition crux::sync)
target_compile_definitions(tls_certificate_store PRIVATE CRUX_TLS_CERTIFICATE_TESTING=1
    _POSIX_C_SOURCE=200809L)
target_compile_options(tls_certificate_store PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME TlsCertificate.tls_certificate_store COMMAND tls_certificate_store)
set_tests_properties(TlsCertificate.tls_certificate_store PROPERTIES TIMEOUT 120)

# Every public and adapter declaration must also compile for a C++ consumer.
add_library(tls_certificate_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(tls_certificate_cpp_headers PRIVATE crux_tls_certificate_adapter)
target_compile_options(tls_certificate_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)
