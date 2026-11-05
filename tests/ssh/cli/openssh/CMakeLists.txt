# The command line on the real wolfSSH provider, on a real socket, for OpenSSH on the host to reach. TEST
# ONLY: tests/ssh/cli/test-openssh.sh drives it, and nothing else builds it. It needs the provider of #192
# and the optional stacks, and it borrows #192's test Access ports and host key generation.
cmake_minimum_required(VERSION 3.20.0)
set(repository "${CMAKE_CURRENT_LIST_DIR}/../../../..")
list(APPEND EXTRA_CONF_FILE "${repository}/components/ssh/ports/zephyr/wolf-ssh.conf")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_cli_openssh_test)

set(provider_test "${repository}/tests/zephyr/ssh_provider/src")
if(NOT EXISTS "${provider_test}/test_access_ports.c")
    message(FATAL_ERROR "This image needs the wolfSSH provider of #192, which is not in this tree")
endif()
# An unsatisfied Kconfig dependency only warns, which would build a different image.
if(NOT CONFIG_CRUX_CLI)
    message(FATAL_ERROR "The command line is not selected")
endif()

set(CRUX_SSH_CLI_COMMAND_LINE ON)
include("${repository}/components/ssh/ports/zephyr/wolf_ssh_settings.cmake")
crux_ssh_wolf_settings()
target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_TESTING=1
    # The provider's stack array and crux::thread's have the same global name, so a real image with the
    # command line, which needs crux::thread, does not link. docs/cli-over-ssh.md reports this. It is for
    # #192 or Thread to fix, and this line does nothing once they have.
    worker_stacks=crux_ssh_wolf_worker_stacks)

target_sources(app PRIVATE
    src/main.c
    "${provider_test}/test_access_ports.c"
    "${provider_test}/test_keys.c"
    "${repository}/components/access/src/access.c"
    "${repository}/components/access/src/access_accounts.c"
    "${repository}/components/access/src/access_delay.c"
    "${repository}/components/access/src/access_login.c"
    "${repository}/components/access/src/access_record.c"
    "${repository}/components/access/src/access_session.c")
target_include_directories(app PRIVATE
    "${provider_test}"
    "${repository}/components/access/include"
    "${repository}/components/access/adapter"
    "${repository}/components/access/src"
    "${repository}/components/cli/include"
    "${repository}/components/cli/src"
    "${repository}/components/ssh/adapter"
    "${repository}/components/ssh/cli"
    "${repository}/components/ssh/ports/zephyr")
target_compile_definitions(app PRIVATE CRUX_SSH_WOLF_TESTING=1)
target_link_libraries(app PRIVATE crux::sync crux_cli_zephyr)
