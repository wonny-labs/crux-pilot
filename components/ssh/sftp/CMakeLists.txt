# Owned by #194. The port in port/ needs wolfSSH headers, so the Host build leaves it out.

# What the callback table links on the Host: the refusing entry points, which need no file service.
include(sources.cmake)
add_library(crux_ssh_sftp STATIC ${CRUX_SSH_SFTP_SOURCES})
target_include_directories(crux_ssh_sftp PRIVATE port)
target_link_libraries(crux_ssh_sftp PRIVATE crux_ssh_adapter)
target_compile_options(crux_ssh_sftp PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The real operations. A consumer of this target also links the file service and an access core,
# which are compositions of their own.
add_library(crux_ssh_sftp_service STATIC crux_ssh_sftp.c)
target_include_directories(crux_ssh_sftp_service PRIVATE port)
target_link_libraries(crux_ssh_sftp_service
    PRIVATE crux_ssh_adapter crux_file_service_api crux_access_adapter)
target_compile_options(crux_ssh_sftp_service PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_ssh_sftp_service PRIVATE c_std_17)
