# Owned by #194. The Host build and the Zephyr composition both read this list.
#
# A composition sets CRUX_SSH_SFTP_FILE_SERVICE before including this file when its image holds the file
# service, and that image then links crux_file_service and an access core. Without it SFTP is refused, so
# that an image with no file service, such as the settings probe, needs no file system to link.
if(CRUX_SSH_SFTP_FILE_SERVICE)
    get_filename_component(_crux_ssh_sftp_components "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(CRUX_SSH_SFTP_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_sftp.c")
    # Include directories the Zephyr composition adds for these sources.
    set(CRUX_SSH_SFTP_INCLUDES
        "${CMAKE_CURRENT_LIST_DIR}/port"
        "${_crux_ssh_sftp_components}/file_service/include"
        "${_crux_ssh_sftp_components}/file/include"
        "${_crux_ssh_sftp_components}/access/adapter")
else()
    set(CRUX_SSH_SFTP_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_sftp_refuse.c")
    set(CRUX_SSH_SFTP_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/port")
endif()
# The WOLFSSH_USER_FILESYSTEM port, compiled with the wolfSSH settings for Zephyr images only. The
# directory of myFilesystem.h goes on the include path of the wolfSSH sources.
set(CRUX_SSH_SFTP_PORT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/port/crux_ssh_sftp_port.c")
set(CRUX_SSH_SFTP_PORT_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/port")
