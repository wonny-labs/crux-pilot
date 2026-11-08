# Owned by #193. The Host build and the Zephyr composition both read this list.
#
# A composition sets CRUX_SSH_CLI_COMMAND_LINE before including this file when its image holds the command
# line, and that image then links the CLI core, an access core and a provider of the CLI's event port. It
# also calls crux_ssh_cli_set_provider(). Without it every session is refused, so that an image with no
# command line, such as the settings probe, links with none of them.
if(CRUX_SSH_CLI_COMMAND_LINE)
    get_filename_component(_crux_ssh_cli_components "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
    set(CRUX_SSH_CLI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_cli.c")
    # Include directories the Zephyr composition adds for these sources.
    set(CRUX_SSH_CLI_INCLUDES
        "${CMAKE_CURRENT_LIST_DIR}"
        "${_crux_ssh_cli_components}/cli/adapter"
        "${_crux_ssh_cli_components}/cli/include"
        "${_crux_ssh_cli_components}/access/adapter")
    # A session's context must have the size the CLI core was built with. The composition compiles this
    # source with no crux_cli_adapter target, so it takes the same settings as that target.
    if(DEFINED CONFIG_CRUX_CLI_LINE_MAX)
        set_property(SOURCE "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_cli.c" APPEND PROPERTY COMPILE_DEFINITIONS
            "CRUX_CLI_LINE_MAX=${CONFIG_CRUX_CLI_LINE_MAX}"
            "CRUX_CLI_TOKENS_MAX=${CONFIG_CRUX_CLI_TOKENS_MAX}")
    endif()
else()
    set(CRUX_SSH_CLI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/crux_ssh_cli_refuse.c")
    set(CRUX_SSH_CLI_INCLUDES "")
endif()
