# The rest of the management services on the native_sim composition, on the same real PSA providers
# (Issue #242): web sessions, the status and configuration routes, the network settings service and
# its routes, the TLS certificate module, the command line and the file service, each with the
# provider of its ports. Include it after composition.cmake and call
# crux_native_management_compose_services() after crux_native_management_compose().
#
# It adds no route: the image names the route sources of the components to crux_web_routes(), and
# scripts/check-composition-image.py --services holds the linked table to exactly those. The
# composition's own code stays under tests/native, and the TLS certificate's composition-only header
# reaches only `app`, which is no public target, so the credential boundary rule is untouched.
#
# A test random generator is refused unless the image declares itself a test image with
# CRUX_NATIVE_MANAGEMENT_TEST_IMAGE, and the C guard in crux_native_management_services.c refuses the
# same at compilation. Entropy on native_sim is test-only: see docs/psa-crypto-design.md.

function(crux_native_management_compose_services)
    if(NOT COMMAND crux_native_management_compose)
        message(FATAL_ERROR "Include composition.cmake and call crux_native_management_compose() first")
    endif()
    if(CONFIG_TEST_RANDOM_GENERATOR)
        message(FATAL_ERROR "The services composition must draw its random values from the PSA "
                            "provider over the entropy driver, not from CONFIG_TEST_RANDOM_GENERATOR")
    endif()
    if(CONFIG_FAKE_ENTROPY_NATIVE_SIM AND NOT CRUX_NATIVE_MANAGEMENT_TEST_IMAGE)
        message(FATAL_ERROR "Test entropy in a build that is not a declared test image: set "
                            "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE only in a native_sim test image")
    endif()
    if(NOT CONFIG_CRUX_WEB OR CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS)
        message(FATAL_ERROR "The services composition serves HTTPS only: CRUX_WEB on, and no plain HTTP")
    endif()
    set(root "${CRUX_NATIVE_MANAGEMENT_ROOT}")
    set(component "${root}/components")
    include("${component}/web/session/sources.cmake")
    include("${component}/web/management/sources.cmake")
    target_sources(app PRIVATE
        "${CRUX_NATIVE_MANAGEMENT_DIR}/crux_native_management_services.c"
        # Web sessions on PSA, and the status, configuration and network routes' helpers.
        ${CRUX_WEB_SESSION_SOURCES} ${CRUX_WEB_SESSION_PSA_PORT}
        ${CRUX_WEB_MANAGEMENT_SOURCES} ${CRUX_WEB_MANAGEMENT_NETWORK_SOURCES}
        # Network settings, with the no-op apply port: nothing is applied to an interface.
        "${component}/network_settings/src/network_settings.c"
        "${component}/network_settings/src/network_settings_check.c"
        "${component}/network_settings/src/network_settings_events.c"
        "${component}/network_settings/src/network_settings_no_apply.c"
        "${component}/network_settings/src/network_settings_cli.c"
        # The TLS certificate module, its PSA port and its event provider.
        "${component}/tls_certificate/src/tls_certificate.c"
        "${component}/tls_certificate/src/tls_x509.c"
        "${component}/tls_certificate/src/tls_certificate_events.c"
        "${component}/tls_certificate/ports/psa/tls_certificate_psa.c"
        # The command line core and its event provider.
        "${component}/cli/src/cli_commands.c"
        "${component}/cli/src/cli_input.c"
        "${component}/cli/src/cli_parse.c"
        "${component}/cli/src/cli_registry.c"
        "${component}/cli/src/cli_session.c"
        "${component}/cli/src/cli_security_events.c"
        # The file service and its event provider.
        "${component}/file_service/src/file_service.c"
        "${component}/file_service/src/file_service_security_events.c")
    target_include_directories(app PRIVATE
        "${CRUX_WEB_SESSION_DIR}" "${CRUX_WEB_MANAGEMENT_DIR}" "${component}/web/src"
        "${component}/network_settings/include" "${component}/network_settings/src"
        "${component}/tls_certificate/include" "${component}/tls_certificate/adapter"
        "${component}/tls_certificate/src"
        "${component}/cli/include" "${component}/cli/adapter" "${component}/cli/src"
        "${component}/file_service/include" "${component}/file_service/src")
    target_link_libraries(app PRIVATE crux_web_adapter)
    target_compile_definitions(app PRIVATE CRUX_NATIVE_MANAGEMENT_SERVICES=1
        CRUX_CLI_LINE_MAX=320 CRUX_CLI_TOKENS_MAX=10)
    if(CRUX_NATIVE_MANAGEMENT_TEST_IMAGE)
        target_compile_definitions(app PRIVATE CRUX_NATIVE_MANAGEMENT_TEST_IMAGE=1)
    endif()
endfunction()
