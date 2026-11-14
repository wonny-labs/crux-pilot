# The management composition on native_sim, on real PSA providers (Issue #242). Include this after
# find_package(Zephyr) and call crux_native_management_compose(). Select a profile with
# EXTRA_CONF_FILE: see tests/native/management_composition/CMakeLists.txt. A later test of the HTTPS
# listener or a login route composes from here.
#
# The components have no Zephyr module integration yet, so their portable sources are compiled into
# the application, as tests/native/credential_fatfs does. They are the product cores: nothing here
# defines a test composition, so a fake port does not link and a test declaration is refused.
set(CRUX_NATIVE_MANAGEMENT_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(CRUX_NATIVE_MANAGEMENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

function(crux_native_management_compose)
    if(NOT CONFIG_BOARD_NATIVE_SIM)
        message(FATAL_ERROR "This composition is restricted to native_sim: its entropy is test-only, "
                            "and its certificate and key are public")
    endif()
    if(NOT CONFIG_FAKE_ENTROPY_NATIVE_SIM)
        message(FATAL_ERROR "The test-only declaration needs review: the entropy source changed")
    endif()
    set(root "${CRUX_NATIVE_MANAGEMENT_ROOT}")
    set(component "${root}/components")
    target_sources(app PRIVATE
        "${CRUX_NATIVE_MANAGEMENT_DIR}/crux_native_management.c"
        # crux_access, with its ports on PSA and Time, and its events and store below.
        "${component}/access/src/access.c"
        "${component}/access/src/access_accounts.c"
        "${component}/access/src/access_delay.c"
        "${component}/access/src/access_login.c"
        "${component}/access/src/access_record.c"
        "${component}/access/src/access_session.c"
        "${component}/access/src/access_clock_time.c"
        "${component}/access/ports/psa/access_psa.c"
        # The store, with its ports on PSA and its device ID from hwinfo.
        "${component}/credential/src/credential_image.c"
        "${component}/credential/src/credential_store.c"
        "${component}/credential/access_store/credential_access_store.c"
        "${component}/credential/ports/psa/credential_psa.c"
        "${component}/credential/ports/zephyr/credential_device_id.c"
        "${component}/credential/src/credential_security_events.c"
        # Security events, and the provider of the Access event port.
        "${component}/security_event/src/security_event.c"
        "${component}/security_event/src/security_event_drain.c"
        "${component}/security_event/src/security_event_fields.c"
        "${component}/security_event/src/security_event_line.c"
        "${component}/security_event/src/security_event_access_port.c"
        # Configuration, with no save provider: decision C6, until #16.
        "${component}/configuration/src/configuration.c"
        "${component}/configuration/src/configuration_text.c"
        "${component}/configuration/src/configuration_no_provider.c"
        "${component}/config_service/src/config_service.c"
        "${component}/config_service/src/config_service_events.c"
        # Status, with every source real.
        "${component}/mgmt_status/src/mgmt_status.c"
        "${component}/mgmt_status/src/mgmt_status_json.c"
        "${component}/mgmt_status/src/source_system.c"
        "${component}/mgmt_status/src/source_time.c"
        "${component}/mgmt_status/src/source_netif.c"
        "${component}/mgmt_status/src/source_credential.c"
        "${component}/mgmt_status/src/source_security_events.c")
    target_include_directories(app PRIVATE
        "${CRUX_NATIVE_MANAGEMENT_DIR}"
        "${root}/tests/native/tls_offload_probe/credentials"
        "${component}/access/include" "${component}/access/adapter" "${component}/access/src"
        "${component}/credential/include" "${component}/credential/adapter"
        "${component}/credential/src" "${component}/credential/ports/psa"
        "${component}/security_event/include" "${component}/security_event/src"
        "${component}/configuration/include" "${component}/configuration/src"
        "${component}/config_service/include" "${component}/config_service/src"
        "${component}/mgmt_status/include" "${component}/mgmt_status/src"
        "${component}/file/ports/zephyr"
        "${component}/interrupt/src")
    target_link_libraries(app PRIVATE crux::file crux::sync crux::thread crux::time crux::logging
        crux::system crux::netif crux_interrupt crux_service_priority_placement)
    target_compile_definitions(app PRIVATE
        CRUX_SECURITY_EVENT_RING=32 CRUX_SECURITY_EVENT_RESERVE=8
        CRUX_SECURITY_EVENT_DRAIN_BURST=4 CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS=100
        CRUX_MGMT_STATUS_REFRESH_MS=1000u)
    target_compile_options(app PRIVATE -Wall -Wextra -Werror)
    if(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
        if(NOT CONFIG_TF_PSA_CRYPTO_USER_CONFIG)
            message(FATAL_ERROR "The Mbed profile needs the core's locks: add mbed-threading.conf")
        endif()
        target_sources(app PRIVATE
            "${CRUX_NATIVE_MANAGEMENT_DIR}/tf_psa_threading/crux_tf_psa_threading.c")
        zephyr_include_directories("${CRUX_NATIVE_MANAGEMENT_DIR}/tf_psa_threading")
    endif()
    if(CONFIG_WOLFPSA)
        # One wolfCrypt under wolfPSA, the socket backend and wolfSSH, so that they agree. The backend
        # rejects a settings file. See components/ssh/ports/zephyr/wolf_ssh_settings.cmake.
        include("${root}/tests/psa_crypto/provider_declarations.cmake")
        target_compile_definitions(wolfSSL INTERFACE
            WOLFSSL_WOLFSSH WOLFSSL_AES_COUNTER WOLFSSL_KEY_GEN WOLFSSL_ED25519_STREAMING_VERIFY
            ${CRUX_PSA_WOLFCRYPT_DEFINITIONS})
    endif()
endfunction()
