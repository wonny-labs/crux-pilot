# A server on Linux that runs the wolfSSH SFTP handlers on the real port and the real file service, so
# that OpenSSH can reach them before the provider of #192 exists. It is a test harness: its login and
# its loop stand in for the provider, and its results say nothing about Zephyr. Only
# scripts/test-ssh-sftp-openssh.sh builds it, because no default workflow fetches wolfSSH.
set(wolfssl "${CRUX_SSH_SFTP_WOLF_MODULES}/wolfssl")
set(wolfssh "${CRUX_SSH_SFTP_WOLF_MODULES}/wolfssh")

set(wolfcrypt_names aes asn coding curve25519 ecc ed25519 error fe_operations ge_operations hash hmac
    kdf logging memory random sha sha256 sha512 signature sp_int wc_port wolfmath)
list(TRANSFORM wolfcrypt_names PREPEND "${wolfssl}/wolfcrypt/src/")
list(TRANSFORM wolfcrypt_names APPEND ".c")
set(wolfssh_names internal io keygen log ossh port ssh wolfsftp)
list(TRANSFORM wolfssh_names PREPEND "${wolfssh}/src/")
list(TRANSFORM wolfssh_names APPEND ".c")

add_library(crux_ssh_sftp_proof_wolf STATIC ${wolfcrypt_names} ${wolfssh_names} proof_seed.c
    ../../../../components/ssh/sftp/port/crux_ssh_sftp_port.c)
target_include_directories(crux_ssh_sftp_proof_wolf SYSTEM PUBLIC
    . "${wolfssl}" "${wolfssh}"
    ../../../../components/ssh/ports/zephyr ../../../../components/ssh/sftp/port)
target_compile_options(crux_ssh_sftp_proof_wolf PRIVATE -O1 -w)
# The very settings file that a Zephyr image reads, by the same route: wolfssh/settings.h reads it under
# WOLFSSH_ZEPHYR, and the file then undefines that. The other two are what the Zephyr module defines.
target_compile_definitions(crux_ssh_sftp_proof_wolf PUBLIC
    WOLFSSL_USER_SETTINGS WOLFSSH_ZEPHYR "CONFIG_WOLFSSH_SETTINGS_FILE=\"crux_wolfssh_settings.h\""
    WOLFSSH_IGNORE_FILE_WARN WOLFSSH_NO_TIMESTAMP _GNU_SOURCE)

add_executable(crux_ssh_sftp_proof_server proof_server.c)
target_link_libraries(crux_ssh_sftp_proof_server PRIVATE crux_ssh_sftp_proof_wolf
    crux_ssh_sftp_service crux_file_service_core crux_access_test_composition crux_file_linux
    crux_file_api crux_ssh_adapter Threads::Threads)
target_include_directories(crux_ssh_sftp_proof_server PRIVATE ../../../../components/file_service/src
    ../../../../components/file/ports/linux)
