# Owned by #193.
include(sources.cmake)
add_library(crux_ssh_cli STATIC ${CRUX_SSH_CLI_SOURCES})
target_link_libraries(crux_ssh_cli PRIVATE crux_ssh_adapter)
target_compile_options(crux_ssh_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
