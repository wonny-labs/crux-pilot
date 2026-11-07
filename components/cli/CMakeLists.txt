set(CRUX_CLI_SOURCES
    src/cli_commands.c
    src/cli_input.c
    src/cli_parse.c
    src/cli_registry.c
    src/cli_session.c
)
list(TRANSFORM CRUX_CLI_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_CLI_SOURCES "${CRUX_CLI_SOURCES}" CACHE INTERNAL "Portable CLI sources")

# Build settings a composition can raise. The core refuses a line too short for config set.
set(CRUX_CLI_LINE_MAX 320 CACHE STRING "Longest input line, in bytes before the line end")
set(CRUX_CLI_TOKENS_MAX 10 CACHE STRING "Most tokens in a line, counting the command's words")

add_library(crux_cli_api INTERFACE)
target_include_directories(crux_cli_api INTERFACE include)
target_link_libraries(crux_cli_api INTERFACE crux_access_api)

# Internal to Crux channel adapters: the serial console and the SSH adapter. Link it PRIVATE, so that
# its include directory never reaches a project. scripts/check-cli-boundary.py tests this.
add_library(crux_cli_adapter INTERFACE)
target_include_directories(crux_cli_adapter INTERFACE adapter)
target_link_libraries(crux_cli_adapter INTERFACE crux_cli_api crux_access_adapter)
target_compile_definitions(crux_cli_adapter INTERFACE
    CRUX_CLI_LINE_MAX=${CRUX_CLI_LINE_MAX}
    CRUX_CLI_TOKENS_MAX=${CRUX_CLI_TOKENS_MAX})

# The core calls the private event port in src/cli_port.h. No provider is linked here, so a product
# image links only once a composition supplies one. The recording fake in tests/cli is a test
# composition that exists only when Crux builds its own tests.
add_library(crux_cli_core STATIC ${CRUX_CLI_SOURCES})
target_include_directories(crux_cli_core PRIVATE src)
target_link_libraries(crux_cli_core PUBLIC crux_cli_api crux::access PRIVATE crux_cli_adapter)
target_compile_options(crux_cli_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_cli INTERFACE)
add_library(crux::cli ALIAS crux_cli)
target_link_libraries(crux_cli INTERFACE crux_cli_api crux_cli_core)

# The serial console, for the composition that owns the console thread. Not a project's target.
if(CRUX_BUILD_SERIAL AND CRUX_BUILD_THREAD)
    add_library(crux_cli_console STATIC console/cli_console.c console/cli_console_serial.c)
    target_include_directories(crux_cli_console PUBLIC console PRIVATE src)
    target_link_libraries(crux_cli_console
        PUBLIC crux_cli_adapter crux::cli crux::serial
        PRIVATE crux::thread)
    target_compile_options(crux_cli_console PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# The provider of crux_cli_port_event over the security events (Issue #242), which a composition
# links by name. An object target, so that a final link always takes it, and a second provider of the
# port is a duplicate symbol. The record never carries the line.
if(CRUX_BUILD_SECURITY_EVENT)
    add_library(crux_cli_security_events OBJECT src/cli_security_events.c)
    target_include_directories(crux_cli_security_events PRIVATE src)
    target_link_libraries(crux_cli_security_events PRIVATE crux_cli_api crux_security_event_api)
    target_compile_options(crux_cli_security_events PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
