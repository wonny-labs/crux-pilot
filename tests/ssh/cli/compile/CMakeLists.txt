# Compile evidence only: the real entry points with Zephyr's flags, and a CLI of the same limits, in a
# workspace with no wolfSSH. The object is not linked, because it needs a provider. tests/ssh/cli/test-compile.sh
# drives this, in the Zephyr CI job. tests/ssh/cli/openssh is the image that runs.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_cli_compile C)
if(NOT CONFIG_CRUX_CLI)
    message(FATAL_ERROR "The command line compile check is not selected")
endif()

get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(CRUX_SSH_CLI_COMMAND_LINE ON)
include("${repository}/components/ssh/cli/sources.cmake")
add_library(crux_ssh_cli_compile OBJECT ${CRUX_SSH_CLI_SOURCES})
target_include_directories(crux_ssh_cli_compile PRIVATE
    "${repository}/components/ssh/src"
    "${repository}/components/ssh/adapter"
    "${repository}/components/access/include"
    "${repository}/components/base/include"
    ${CRUX_SSH_CLI_INCLUDES})
target_link_libraries(crux_ssh_cli_compile PRIVATE zephyr_interface)
add_dependencies(crux_ssh_cli_compile zephyr_generated_headers)
target_compile_options(crux_ssh_cli_compile PRIVATE -Wall -Wextra -Werror)

target_sources(app PRIVATE main.c)
add_dependencies(app crux_ssh_cli_compile)
