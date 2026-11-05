# The command line on Zephyr: compile evidence for now. Access has no Zephyr composition, and Issue
# #182 no provider of the event port, so nothing here is linked into an image: a composition that
# calls these functions has to supply them. No Serial wiring is exercised, and no board is operated.
get_filename_component(CRUX_CLI_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_ACCESS_COMPONENT_DIR "${CRUX_CLI_COMPONENT_DIR}/../access" ABSOLUTE)

add_library(crux_cli_api INTERFACE)
target_include_directories(crux_cli_api INTERFACE
    "${CRUX_CLI_COMPONENT_DIR}/include" "${CRUX_ACCESS_COMPONENT_DIR}/include")
target_link_libraries(crux_cli_api INTERFACE crux::base)
target_compile_options(crux_cli_api INTERFACE ${TOOLCHAIN_C_FLAGS})

add_library(crux_cli_adapter INTERFACE)
target_include_directories(crux_cli_adapter INTERFACE
    "${CRUX_CLI_COMPONENT_DIR}/adapter" "${CRUX_ACCESS_COMPONENT_DIR}/adapter")
target_link_libraries(crux_cli_adapter INTERFACE crux_cli_api)
target_compile_definitions(crux_cli_adapter INTERFACE
    CRUX_CLI_LINE_MAX=${CONFIG_CRUX_CLI_LINE_MAX}
    CRUX_CLI_TOKENS_MAX=${CONFIG_CRUX_CLI_TOKENS_MAX})

add_library(crux_cli_zephyr STATIC
    ../../src/cli_commands.c
    ../../src/cli_input.c
    ../../src/cli_parse.c
    ../../src/cli_registry.c
    ../../src/cli_session.c
    ../../console/cli_console.c
    ../../console/cli_console_serial.c
    cli_shell_rule.c
)
target_include_directories(crux_cli_zephyr PRIVATE ../../src ../../console)
target_link_libraries(crux_cli_zephyr PRIVATE crux_cli_adapter crux::serial crux::thread zephyr_interface)
target_compile_options(crux_cli_zephyr PRIVATE -Wall -Wextra -Werror)

add_library(crux_cli INTERFACE)
add_library(crux::cli ALIAS crux_cli)
target_link_libraries(crux_cli INTERFACE crux_cli_api)
cmake_language(DEFER CALL crux_assert_portable_usage crux::cli)
