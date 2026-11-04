# Owned by #194. The port in port/ needs wolfSSH headers, so the Host build leaves it out.
include(sources.cmake)
add_library(crux_ssh_sftp STATIC ${CRUX_SSH_SFTP_SOURCES})
target_link_libraries(crux_ssh_sftp PRIVATE crux_ssh_adapter)
target_compile_options(crux_ssh_sftp PRIVATE -Wall -Wextra -Wpedantic -Werror)
