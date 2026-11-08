# Owned by #192. The wolfSSH provider's sources. crux_ssh_wolf_settings() builds them as Crux-owned code
# with the wolfSSH settings.
# The wire checks hold no wolfSSH type, and the Host tests them: tests/ssh/wire.
set(CRUX_SSH_PROVIDER_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_wolf_provider.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/crux_ssh_wire.c")
set(CRUX_SSH_PROVIDER_INCLUDES "${CMAKE_CURRENT_LIST_DIR}")

# The design gives the Mbed profile no SSH provider, and two TLS stacks are not a supported image.
if(CONFIG_MBEDTLS OR CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
    message(FATAL_ERROR "CLI and SFTP need the Wolf profile. The Mbed profile has no SSH provider: "
                        "see docs/plans/secure-management-design.md#provider-profiles")
endif()
