# Owned by #192.
include(sources.cmake)
add_library(crux_ssh_access STATIC ${CRUX_SSH_ACCESS_SOURCES})
target_link_libraries(crux_ssh_access PRIVATE crux_ssh_adapter crux_access_adapter)
target_compile_options(crux_ssh_access PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Apart from the entry points, so that an image can log in with no credential store in it.
add_library(crux_ssh_host_key STATIC ${CRUX_SSH_HOST_KEY_SOURCES})
target_include_directories(crux_ssh_host_key PUBLIC .)
target_link_libraries(crux_ssh_host_key PUBLIC crux_ssh_provider PRIVATE crux_credential_adapter)
target_compile_options(crux_ssh_host_key PRIVATE -Wall -Wextra -Wpedantic -Werror)
