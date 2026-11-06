# A configure-only probe of the adapter boundary. A consumer that reaches crux_tls_certificate_adapter
# must be refused, as one that reaches the credential adapter is, and a consumer of the public target
# must be accepted. TlsCertificate.boundary_* configure it both ways.
cmake_minimum_required(VERSION 3.28)
project(tls_certificate_boundary_probe LANGUAGES C)

get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
foreach(component IN ITEMS TIME THREAD SYNC QUEUE NETIF SOCKETS CONFIGURATION FILE ACCESS CREDENTIAL
        TLS_CERTIFICATE)
    set(CRUX_BUILD_${component} ON CACHE BOOL "" FORCE)
endforeach()
set(CRUX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CRUX_TIME_PROVIDER linux CACHE STRING "" FORCE)
add_subdirectory("${CRUX_ROOT}" crux)

add_library(probe_consumer INTERFACE)
if(PROBE_LEAK)
    target_link_libraries(probe_consumer INTERFACE crux_tls_certificate_adapter)
else()
    target_link_libraries(probe_consumer INTERFACE crux::tls_certificate)
endif()
cmake_language(DEFER CALL crux_assert_credential_adapter_unreachable probe_consumer)
