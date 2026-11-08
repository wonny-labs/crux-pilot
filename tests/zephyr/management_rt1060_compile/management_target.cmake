# SPDX-License-Identifier: Apache-2.0
# The source lists of tests/native/management_composition (composition.cmake, services.cmake, ssh.cmake), for
# RT1060-EVKB, compile and link only. Those three files refuse a board on purpose: their entropy, host key and
# certificate are test forms for native_sim. This one does not edit them. It repeats their source lists, and
# replaces their guards with the ones below, which allow only a declared compile-only image. A change to a list
# in the native files must be repeated here: tests/build/test_management_target_lists.py compares them.
set(CRUX_MGMT_NATIVE_DIR "${CRUX_ROOT}/tests/native/management_composition/composition")

function(crux_management_target_compose)
    if(NOT CONFIG_BOARD_MIMXRT1060_EVK_MIMXRT1062_QSPI OR NOT CONFIG_BOARD_REVISION_B)
        message(FATAL_ERROR "This compile vehicle is for RT1060-EVKB revision B, QSPI")
    endif()
    if(NOT CONFIG_CRUX_LOGGING)
        message(FATAL_ERROR "The security events write to Crux Logging: CONFIG_CRUX_LOGGING is required")
    endif()
    if(CONFIG_TEST_RANDOM_GENERATOR OR CONFIG_FAKE_ENTROPY_NATIVE_SIM)
        message(FATAL_ERROR "A test random generator or fake entropy: this composition draws its random values "
                            "from the PSA provider over the TRNG driver")
    endif()
    if(NOT CONFIG_ENTROPY_MCUX_TRNG OR NOT CONFIG_WOLFPSA OR CONFIG_MBEDTLS OR CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
        message(FATAL_ERROR "The Wolf profile on the TRNG is required: CONFIG_ENTROPY_MCUX_TRNG, CONFIG_WOLFPSA")
    endif()
    if(NOT CONFIG_CRUX_SSH OR NOT CONFIG_CRUX_SSH_CLI OR NOT CONFIG_CRUX_SSH_SFTP OR
       NOT CONFIG_CRUX_WEB OR CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS)
        message(FATAL_ERROR "HTTPS only, and SSH with CLI and SFTP, are required")
    endif()
    set(root "${CRUX_ROOT}")
    set(component "${root}/components")
    include("${component}/web/session/sources.cmake")
    include("${component}/web/management/sources.cmake")
    target_sources(app PRIVATE
        # Copies of crux_native_management.c and crux_native_management_services.c, which include the declared
        # test certificate or call it, and its header refuses a board. See the head of each copy.
        "${CMAKE_CURRENT_LIST_DIR}/src/mgmt_start.c"
        "${CMAKE_CURRENT_LIST_DIR}/src/mgmt_services.c"
        "${component}/access/src/access.c"
        "${component}/access/src/access_accounts.c"
        "${component}/access/src/access_delay.c"
        "${component}/access/src/access_login.c"
        "${component}/access/src/access_record.c"
        "${component}/access/src/access_session.c"
        "${component}/access/src/access_clock_time.c"
        "${component}/access/ports/psa/access_psa.c"
        "${component}/credential/src/credential_image.c"
        "${component}/credential/src/credential_store.c"
        "${component}/credential/access_store/credential_access_store.c"
        "${component}/credential/ports/psa/credential_psa.c"
        "${component}/credential/ports/zephyr/credential_device_id.c"
        "${component}/credential/src/credential_security_events.c"
        "${component}/security_event/src/security_event.c"
        "${component}/security_event/src/security_event_drain.c"
        "${component}/security_event/src/security_event_fields.c"
        "${component}/security_event/src/security_event_line.c"
        "${component}/security_event/src/security_event_access_port.c"
        "${component}/configuration/src/configuration.c"
        "${component}/configuration/src/configuration_text.c"
        "${component}/configuration/src/configuration_no_provider.c"
        "${component}/config_service/src/config_service.c"
        "${component}/config_service/src/config_service_events.c"
        "${component}/mgmt_status/src/mgmt_status.c"
        "${component}/mgmt_status/src/mgmt_status_json.c"
        "${component}/mgmt_status/src/source_system.c"
        "${component}/mgmt_status/src/source_time.c"
        "${component}/mgmt_status/src/source_netif.c"
        "${component}/mgmt_status/src/source_credential.c"
        "${component}/mgmt_status/src/source_security_events.c"
        ${CRUX_WEB_SESSION_SOURCES} ${CRUX_WEB_SESSION_PSA_PORT}
        ${CRUX_WEB_MANAGEMENT_SOURCES} ${CRUX_WEB_MANAGEMENT_NETWORK_SOURCES}
        "${component}/network_settings/src/network_settings.c"
        "${component}/network_settings/src/network_settings_check.c"
        "${component}/network_settings/src/network_settings_events.c"
        "${component}/network_settings/src/network_settings_no_apply.c"
        "${component}/network_settings/src/network_settings_cli.c"
        "${component}/tls_certificate/src/tls_certificate.c"
        "${component}/tls_certificate/src/tls_x509.c"
        "${component}/tls_certificate/src/tls_certificate_events.c"
        "${component}/tls_certificate/ports/psa/tls_certificate_psa.c"
        "${component}/cli/src/cli_commands.c"
        "${component}/cli/src/cli_input.c"
        "${component}/cli/src/cli_parse.c"
        "${component}/cli/src/cli_registry.c"
        "${component}/cli/src/cli_session.c"
        "${component}/cli/src/cli_security_events.c"
        "${component}/file_service/src/file_service.c"
        "${component}/file_service/src/file_service_security_events.c")
    target_include_directories(app PRIVATE
        "${CRUX_MGMT_NATIVE_DIR}"
        "${root}/tests/native/tls_offload_probe/credentials"
        "${component}/access/include" "${component}/access/adapter" "${component}/access/src"
        "${component}/credential/include" "${component}/credential/adapter"
        "${component}/credential/src" "${component}/credential/ports/psa"
        "${component}/security_event/include" "${component}/security_event/src"
        "${component}/configuration/include" "${component}/configuration/src"
        "${component}/config_service/include" "${component}/config_service/src"
        "${component}/mgmt_status/include" "${component}/mgmt_status/src"
        "${component}/file/ports/zephyr"
        "${component}/interrupt/src"
        "${CRUX_WEB_SESSION_DIR}" "${CRUX_WEB_MANAGEMENT_DIR}" "${component}/web/src"
        "${component}/network_settings/include" "${component}/network_settings/src"
        "${component}/tls_certificate/include" "${component}/tls_certificate/adapter"
        "${component}/tls_certificate/src"
        "${component}/cli/include" "${component}/cli/adapter" "${component}/cli/src"
        "${component}/file_service/include" "${component}/file_service/src")
    target_link_libraries(app PRIVATE crux::file crux::sync crux::thread crux::time
        crux::logging crux::system crux::netif crux_interrupt crux_web_adapter)
    target_compile_definitions(app PRIVATE
        CRUX_SECURITY_EVENT_RING=32 CRUX_SECURITY_EVENT_RESERVE=8
        CRUX_SECURITY_EVENT_DRAIN_BURST=4 CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS=100
        CRUX_MGMT_STATUS_REFRESH_MS=1000u
        CRUX_NATIVE_MANAGEMENT_SERVICES=1 CRUX_CLI_LINE_MAX=320 CRUX_CLI_TOKENS_MAX=10
        CRUX_NATIVE_MANAGEMENT_SSH=1 CRUX_MGMT_RT1060_COMPILE_ONLY=1)
    target_compile_options(app PRIVATE -Wall -Wextra -Werror)

    # One wolfCrypt under wolfPSA, the socket backend and wolfSSH, so that they agree.
    include("${root}/tests/psa_crypto/provider_declarations.cmake")
    target_compile_definitions(wolfSSL INTERFACE
        WOLFSSL_WOLFSSH WOLFSSL_AES_COUNTER WOLFSSL_KEY_GEN WOLFSSL_ED25519_STREAMING_VERIFY
        ${CRUX_PSA_WOLFCRYPT_DEFINITIONS})

    include("${root}/components/ssh/ports/zephyr/wolf_ssh_settings.cmake")
    crux_ssh_wolf_settings()
    target_compile_definitions(crux_ssh_entries PRIVATE CRUX_CLI_LINE_MAX=320 CRUX_CLI_TOKENS_MAX=10)
    target_sources(app PRIVATE ${CRUX_SSH_HOST_KEY_SOURCES})
    target_include_directories(app PRIVATE
        ${CRUX_SSH_HOST_KEY_INCLUDES}
        "${root}/components/ssh/adapter" "${root}/components/ssh/cli"
        "${root}/components/ssh/ports/zephyr")
endfunction()
