# Owned by #192. The Host build and the Zephyr composition both read this list.
set(CRUX_SSH_ACCESS_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_access.c")
# Include directories the Zephyr composition adds for these sources.
get_filename_component(_crux_ssh_repository "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(CRUX_SSH_ACCESS_INCLUDES
    "${CMAKE_CURRENT_LIST_DIR}" "${_crux_ssh_repository}/components/access/adapter")
# A composition with a credential store adds this source and these directories.
set(CRUX_SSH_HOST_KEY_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_host_key.c")
set(CRUX_SSH_HOST_KEY_INCLUDES
    "${CMAKE_CURRENT_LIST_DIR}" "${_crux_ssh_repository}/components/credential/adapter"
    "${_crux_ssh_repository}/components/credential/include")
