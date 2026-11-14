set(CRUX_TLS_CERTIFICATE_SOURCES
    src/tls_certificate.c
    src/tls_x509.c
)
list(TRANSFORM CRUX_TLS_CERTIFICATE_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_TLS_CERTIFICATE_SOURCES "${CRUX_TLS_CERTIFICATE_SOURCES}" CACHE INTERNAL
    "Portable TLS certificate sources")

add_library(crux_tls_certificate_api INTERFACE)
target_include_directories(crux_tls_certificate_api INTERFACE include)
target_link_libraries(crux_tls_certificate_api INTERFACE crux_access_api)

# For a composition and its TLS provider only, always PRIVATE. It carries the device's private key as
# PKCS #8 bytes, so it links the credential adapter on purpose: cmake/CruxCredentialBoundary.cmake
# then refuses any public target or project that reaches this header, as it refuses that one.
add_library(crux_tls_certificate_adapter INTERFACE)
target_include_directories(crux_tls_certificate_adapter INTERFACE adapter)
target_link_libraries(crux_tls_certificate_adapter INTERFACE crux_tls_certificate_api
    crux_credential_adapter)

# The core calls the private ports in src/tls_certificate_port.h, the credential adapter and Access.
# No provider of the ports is linked here, so a product image links only once a composition supplies
# them: ports/psa for hashing, signing and random bytes, and the event provider below.
add_library(crux_tls_certificate_core STATIC ${CRUX_TLS_CERTIFICATE_SOURCES})
target_include_directories(crux_tls_certificate_core PRIVATE src adapter)
target_link_libraries(crux_tls_certificate_core
    PUBLIC crux_tls_certificate_api PRIVATE crux_credential_adapter crux::sync)
target_compile_options(crux_tls_certificate_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_tls_certificate INTERFACE)
add_library(crux::tls_certificate ALIAS crux_tls_certificate)
target_link_libraries(crux_tls_certificate INTERFACE crux_tls_certificate_core crux::access
    crux::credential_store)

# The provider of crux_tls_certificate_port_event, which a composition links by name. It is an object
# target, so that a final link always takes it, and a second provider of the port is a duplicate
# symbol.
if(TARGET crux_security_event_api)
    add_library(crux_tls_certificate_security_events OBJECT src/tls_certificate_events.c)
    target_include_directories(crux_tls_certificate_security_events PRIVATE src)
    target_link_libraries(crux_tls_certificate_security_events
        PRIVATE crux_tls_certificate_api crux_security_event_api)
    target_compile_options(crux_tls_certificate_security_events PRIVATE -Wall -Wextra -Wpedantic
        -Werror)
endif()
