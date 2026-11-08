# Owned by #193.

# What the callback table links on the Host: the refusing entry points, which need no command line.
include(sources.cmake)
add_library(crux_ssh_cli STATIC ${CRUX_SSH_CLI_SOURCES})
target_link_libraries(crux_ssh_cli PRIVATE crux_ssh_adapter)
target_compile_options(crux_ssh_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The real entry points, in a build that has the command line. A consumer of this target also links a CLI
# core and an access core, which are compositions of their own.
if(TARGET crux_cli_adapter)
    add_library(crux_ssh_cli_sessions STATIC crux_ssh_cli.c)
    target_link_libraries(crux_ssh_cli_sessions PRIVATE crux_ssh_adapter crux_cli_adapter)
    target_compile_options(crux_ssh_cli_sessions PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_features(crux_ssh_cli_sessions PRIVATE c_std_17)
endif()
