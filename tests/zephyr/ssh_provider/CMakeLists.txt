# The wolfSSH provider on a real socket: an image for OpenSSH on the host to reach, and the conformance
# cases of tests/ssh with a scripted client in the same image. scripts/test-ssh-provider.sh drives it.
# Test only. The Access ports here are fakes: see src/test_access_ports.c.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND EXTRA_CONF_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/ssh/ports/zephyr/wolf-ssh.conf")
# The host key from the credential store on its PSA ports, in place of a key generated at each boot.
option(CRUX_SSH_TEST_STORED_HOST_KEY "Serve the host key from the credential store" OFF)
if(CRUX_SSH_TEST_STORED_HOST_KEY)
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/stored-host-key.conf")
    list(APPEND EXTRA_DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/stored-host-key.overlay")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_provider_test)

include(../../../components/ssh/ports/zephyr/wolf_ssh_settings.cmake)
crux_ssh_wolf_settings()
target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_TESTING=1)
if(DEFINED CRUX_SSH_TEST_WINDOW_BYTES)
    target_compile_definitions(crux_ssh_wolf_provider PRIVATE
        CRUX_SSH_WOLF_WINDOW_BYTES=${CRUX_SSH_TEST_WINDOW_BYTES})
endif()
if(CRUX_SSH_TEST_OFFER_MLKEM)
    target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_OFFER_MLKEM=1)
endif()

set(repository "${CMAKE_CURRENT_LIST_DIR}/../../..")
target_sources(app PRIVATE
    src/main.c
    src/test_access_ports.c
    src/test_keys.c
    src/test_sessions.c
    "${repository}/components/access/src/access.c"
    "${repository}/components/access/src/access_accounts.c"
    "${repository}/components/access/src/access_delay.c"
    "${repository}/components/access/src/access_login.c"
    "${repository}/components/access/src/access_record.c"
    "${repository}/components/access/src/access_session.c")
target_include_directories(app PRIVATE
    src
    "${repository}/components/access/include"
    "${repository}/components/access/adapter"
    "${repository}/components/access/src"
    "${repository}/components/ssh/adapter"
    "${repository}/components/ssh/ports/zephyr")
target_compile_definitions(app PRIVATE CRUX_SSH_WOLF_TESTING=1)
target_link_libraries(app PRIVATE crux::sync)

if(CRUX_SSH_TEST_STORED_HOST_KEY)
    if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_WOLFPSA)
        message(FATAL_ERROR "The stored host key image is for native_sim on wolfPSA: its random "
                            "numbers come from a test generator")
    endif()
    # One wolfCrypt under wolfPSA and wolfSSH, as in tests/native/management_composition.
    include("${repository}/tests/psa_crypto/provider_declarations.cmake")
    target_compile_definitions(wolfSSL INTERFACE ${CRUX_PSA_WOLFCRYPT_DEFINITIONS})
    include("${repository}/components/ssh/access/sources.cmake")
    target_sources(app PRIVATE
        src/stored_host_key.c
        ${CRUX_SSH_HOST_KEY_SOURCES}
        "${repository}/components/credential/src/credential_image.c"
        "${repository}/components/credential/src/credential_store.c"
        "${repository}/components/credential/ports/psa/credential_psa.c"
        "${repository}/components/credential/ports/zephyr/credential_device_id.c")
    target_include_directories(app PRIVATE
        ${CRUX_SSH_HOST_KEY_INCLUDES}
        "${repository}/components/credential/src"
        "${repository}/components/credential/ports/psa"
        "${repository}/components/file/ports/zephyr")
    target_compile_definitions(app PRIVATE CRUX_SSH_TEST_STORED_HOST_KEY=1)
    target_link_libraries(app PRIVATE crux::file)
endif()

# The scripted client and the cases run on the host-reachable socket, so only on native_sim.
if(CONFIG_ARCH_POSIX)
    target_sources(app PRIVATE src/conformance_client.c "${repository}/tests/ssh/conformance.c")
    target_include_directories(app PRIVATE "${repository}/tests/ssh")
endif()
