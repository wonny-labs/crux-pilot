# Applies the settings that wolfSSH needs from wolfCrypt, for every translation unit in the image, so
# that wolfSSL, the TLS socket backend and wolfSSH agree. The module's user_settings.h leaves each of
# these undefined, so none of them redefines a macro there. Call it after find_package(Zephyr).
set(CRUX_SSH_WOLF_SETTINGS_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(crux_ssh_wolf_settings)
    if(NOT TARGET wolfSSL)
        message(FATAL_ERROR "The wolfSSH provider needs the crux-secure-management west group and "
                            "CONFIG_WOLFSSL=y")
    endif()
    target_compile_definitions(wolfSSL INTERFACE
        WOLFSSL_WOLFSSH                  # wc_SSH_KDF and the SSH key formats
        WOLFSSL_AES_COUNTER              # aes128-ctr
        WOLFSSL_KEY_GEN                  # Host key generation
        WOLFSSL_ED25519_STREAMING_VERIFY # wolfSSH disables ssh-ed25519 without it
    )
    target_include_directories(app PRIVATE "${CRUX_SSH_WOLF_SETTINGS_DIR}")
endfunction()

# For a Crux library that includes wolfSSH headers, such as the provider. The module gives its definitions
# only to the app target, and wolfssh/settings.h reads the Crux settings only under WOLFSSH_ZEPHYR, so
# without this a library would see different wolfSSH settings, with no error.
function(crux_ssh_wolfssh_client target)
    target_compile_definitions(${target} PRIVATE
        WOLFSSH_ZEPHYR WOLFSSH_IGNORE_FILE_WARN WOLFSSH_NO_TIMESTAMP)
    target_include_directories(${target} PRIVATE "${CRUX_SSH_WOLF_SETTINGS_DIR}")
endfunction()
