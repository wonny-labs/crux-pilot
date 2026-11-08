# SSH beside the HTTPS services on the native_sim composition (Issue #197): the wolfSSH provider with the
# command line over SSH and SFTP, selected by CONFIG_CRUX_SSH (zephyr/Kconfig), on the same Access core,
# credential store and file service. Include it after services.cmake and call
# crux_native_management_compose_ssh() after crux_native_management_compose_services().
#
# The host key is held in test form, so this composition is for a declared native_sim test image only:
# CRUX_NATIVE_MANAGEMENT_TEST_IMAGE, as for the test entropy of services.cmake. Any other build is refused
# here at configuration, and ssh_test_host_key_guard.h refuses the same at compilation. No product host key
# handling is defined: see docs/ssh-integrated-image.md.

function(crux_native_management_compose_ssh)
    if(NOT COMMAND crux_native_management_compose_services)
        message(FATAL_ERROR "Include services.cmake first: SSH reaches the CLI core and the file service "
                            "that it composes")
    endif()
    if(NOT CONFIG_BOARD_NATIVE_SIM)
        message(FATAL_ERROR "The SSH composition is restricted to native_sim: its host key is held in test "
                            "form and its entropy is test-only")
    endif()
    if(NOT CRUX_NATIVE_MANAGEMENT_TEST_IMAGE)
        message(FATAL_ERROR "The SSH composition holds the host key in test form, in a build that is not a "
                            "declared test image: set CRUX_NATIVE_MANAGEMENT_TEST_IMAGE only in a native_sim "
                            "test image")
    endif()
    if(CONFIG_TEST_RANDOM_GENERATOR)
        message(FATAL_ERROR "The SSH composition must draw its random values from the PSA provider over the "
                            "entropy driver, not from CONFIG_TEST_RANDOM_GENERATOR")
    endif()
    if(CONFIG_MBEDTLS OR CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
        message(FATAL_ERROR "CLI and SFTP need the Wolf profile. The Mbed profile has no SSH provider: "
                            "see docs/plans/secure-management-design.md#provider-profiles")
    endif()
    if(NOT CONFIG_CRUX_SSH)
        message(FATAL_ERROR "CONFIG_CRUX_SSH is not selected. It needs the crux-secure-management west group "
                            "(wolfSSH) and the Wolf profile")
    endif()
    if(NOT CONFIG_CRUX_SSH_CLI OR NOT CONFIG_CRUX_SSH_SFTP)
        message(FATAL_ERROR "This composition has the CLI core and the file service, so it selects both "
                            "CONFIG_CRUX_SSH_CLI and CONFIG_CRUX_SSH_SFTP")
    endif()

    set(root "${CRUX_NATIVE_MANAGEMENT_ROOT}")
    include("${root}/components/ssh/ports/zephyr/wolf_ssh_settings.cmake")
    crux_ssh_wolf_settings()
    # Test image: the provider's stack probe and its test hooks, which only this declaration allows.
    target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_TESTING=1)
    # The entry points build apart from the application, so they take the CLI core's settings from here.
    target_compile_definitions(crux_ssh_entries PRIVATE CRUX_CLI_LINE_MAX=320 CRUX_CLI_TOKENS_MAX=10)

    target_sources(app PRIVATE
        "${CRUX_NATIVE_MANAGEMENT_DIR}/crux_native_management_ssh.c"
        ${CRUX_SSH_HOST_KEY_SOURCES})
    target_include_directories(app PRIVATE
        ${CRUX_SSH_HOST_KEY_INCLUDES}
        "${root}/components/ssh/adapter" "${root}/components/ssh/cli"
        "${root}/components/ssh/ports/zephyr")
    target_compile_definitions(app PRIVATE CRUX_NATIVE_MANAGEMENT_SSH=1 CRUX_SSH_WOLF_TESTING=1)
    target_link_libraries(app PRIVATE crux::sync)
endfunction()
