# Owned by #192.
include(sources.cmake)
add_library(crux_ssh_access STATIC ${CRUX_SSH_ACCESS_SOURCES})
target_link_libraries(crux_ssh_access PRIVATE crux_ssh_adapter)
target_compile_options(crux_ssh_access PRIVATE -Wall -Wextra -Wpedantic -Werror)
