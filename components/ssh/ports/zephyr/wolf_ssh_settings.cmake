# The Zephyr composition of SSH. Call crux_ssh_wolf_settings() after find_package(Zephyr).
#
# It applies what wolfSSH needs from wolfCrypt to every translation unit, so that wolfSSL, the TLS socket
# backend and wolfSSH agree. The module's user_settings.h leaves each of these undefined, so none of them
# redefines a macro there. It also builds the entry points, the file port and the provider from the lists
# that each session owns, so that no session changes this file to add a source.
set(CRUX_SSH_WOLF_SETTINGS_DIR "${CMAKE_CURRENT_LIST_DIR}")
get_filename_component(CRUX_SSH_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_REPOSITORY_DIR "${CRUX_SSH_DIR}/../.." ABSOLUTE)
include("${CRUX_SSH_DIR}/access/sources.cmake")
include("${CRUX_SSH_DIR}/cli/sources.cmake")
include("${CRUX_SSH_DIR}/sftp/sources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/provider_sources.cmake")

# For a Crux library that includes wolfSSH headers, such as the provider. The module gives its definitions
# only to the app target, and wolfssh/settings.h reads the Crux settings only under WOLFSSH_ZEPHYR, so
# without this a library would see different wolfSSH settings, with no error.
function(crux_ssh_wolfssh_client target)
    target_compile_definitions(${target} PRIVATE
        WOLFSSH_ZEPHYR WOLFSSH_IGNORE_FILE_WARN WOLFSSH_NO_TIMESTAMP)
    target_include_directories(${target} PRIVATE
        "${CRUX_SSH_WOLF_SETTINGS_DIR}" "${CRUX_SSH_SFTP_PORT_INCLUDE}")
endfunction()

function(_crux_ssh_owned_objects target)
    add_library(${target} OBJECT ${ARGN})
    target_link_libraries(${target} PRIVATE zephyr_interface)
    add_dependencies(${target} zephyr_generated_headers)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    target_sources(app PRIVATE $<TARGET_OBJECTS:${target}>)
endfunction()

function(crux_ssh_wolf_settings)
    if(NOT TARGET wolfSSL)
        message(FATAL_ERROR "The wolfSSH provider needs the crux-secure-management west group and "
                            "CONFIG_WOLFSSL=y")
    endif()
    target_compile_definitions(wolfSSL INTERFACE
        WOLFSSL_WOLFSSH                  # The SSH key formats and KDF in wolfCrypt
        WOLFSSL_AES_COUNTER              # aes128-ctr
        WOLFSSL_KEY_GEN                  # Host key generation
        WOLFSSL_ED25519_STREAMING_VERIFY # wolfSSH disables ssh-ed25519 without it
    )
    # The wolfSSH sources are in the app target, as its module adds them there.
    target_include_directories(app PRIVATE
        "${CRUX_SSH_WOLF_SETTINGS_DIR}"
        "${CRUX_SSH_SFTP_PORT_INCLUDE}"
        "${CRUX_SSH_DIR}/src"
        "${CRUX_REPOSITORY_DIR}/components/base/include"
    )

    # The entry points and the table. They use no wolfSSH header.
    _crux_ssh_owned_objects(crux_ssh_entries
        "${CRUX_SSH_DIR}/src/crux_ssh_callbacks.c"
        ${CRUX_SSH_ACCESS_SOURCES} ${CRUX_SSH_CLI_SOURCES} ${CRUX_SSH_SFTP_SOURCES})
    target_include_directories(crux_ssh_entries PRIVATE
        "${CRUX_SSH_DIR}/src" "${CRUX_SSH_DIR}/adapter"
        "${CRUX_REPOSITORY_DIR}/components/access/include"
        "${CRUX_REPOSITORY_DIR}/components/base/include"
        ${CRUX_SSH_ACCESS_INCLUDES} ${CRUX_SSH_CLI_INCLUDES} ${CRUX_SSH_SFTP_INCLUDES})

    # The file port, built with the wolfSSH settings.
    _crux_ssh_owned_objects(crux_ssh_sftp_port ${CRUX_SSH_SFTP_PORT_SOURCES})
    crux_ssh_wolfssh_client(crux_ssh_sftp_port)

    # The provider, once #192 lists its sources.
    if(CRUX_SSH_PROVIDER_SOURCES)
        _crux_ssh_owned_objects(crux_ssh_wolf_provider ${CRUX_SSH_PROVIDER_SOURCES})
        crux_ssh_wolfssh_client(crux_ssh_wolf_provider)
        target_include_directories(crux_ssh_wolf_provider PRIVATE
            "${CRUX_SSH_DIR}/src" "${CRUX_SSH_DIR}/adapter"
            "${CRUX_REPOSITORY_DIR}/components/access/include"
            "${CRUX_REPOSITORY_DIR}/components/base/include"
            ${CRUX_SSH_PROVIDER_INCLUDES})
    endif()
endfunction()
