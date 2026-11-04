# Owned by #194. The Host build and the Zephyr composition both read this list.
set(CRUX_SSH_SFTP_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_sftp.c")
# Include directories the Zephyr composition adds for these sources.
set(CRUX_SSH_SFTP_INCLUDES "")
# The WOLFSSH_USER_FILESYSTEM port, compiled with the wolfSSH settings for Zephyr images only. The
# directory of myFilesystem.h goes on the include path of the wolfSSH sources.
set(CRUX_SSH_SFTP_PORT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/port/crux_ssh_sftp_port.c")
set(CRUX_SSH_SFTP_PORT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/port")
