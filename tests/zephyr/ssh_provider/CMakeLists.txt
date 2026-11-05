# The wolfSSH provider on a real socket: an image for OpenSSH on the host to reach, and the conformance
# cases of tests/ssh with a scripted client in the same image. scripts/test-ssh-provider.sh drives it.
# Test only. The Access ports here are fakes: see src/test_access_ports.c.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND EXTRA_CONF_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/ssh/ports/zephyr/wolf-ssh.conf")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_provider_test)

include(../../../components/ssh/ports/zephyr/wolf_ssh_settings.cmake)
crux_ssh_wolf_settings()
target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_TESTING=1)
if(DEFINED CRUX_SSH_TEST_WINDOW_BYTES)
    target_compile_definitions(crux_ssh_wolf_provider PRIVATE
        CRUX_SSH_WOLF_WINDOW_BYTES=${CRUX_SSH_TEST_WINDOW_BYTES})
endif()
if(CRUX_SSH_TEST_OFFER_MLKEM)
    target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_OFFER_MLKEM=1)
endif()

set(repository "${CMAKE_CURRENT_LIST_DIR}/../../..")
target_sources(app PRIVATE
    src/main.c
    src/test_access_ports.c
    src/test_keys.c
    src/test_sessions.c
    "${repository}/components/access/src/access.c"
    "${repository}/components/access/src/access_accounts.c"
    "${repository}/components/access/src/access_delay.c"
    "${repository}/components/access/src/access_login.c"
    "${repository}/components/access/src/access_record.c"
    "${repository}/components/access/src/access_session.c")
target_include_directories(app PRIVATE
    src
    "${repository}/components/access/include"
    "${repository}/components/access/adapter"
    "${repository}/components/access/src"
    "${repository}/components/ssh/adapter"
    "${repository}/components/ssh/ports/zephyr")
target_compile_definitions(app PRIVATE CRUX_SSH_WOLF_TESTING=1)
target_link_libraries(app PRIVATE crux::sync)

# The scripted client and the cases run on the host-reachable socket, so only on native_sim.
if(CONFIG_ARCH_POSIX)
    target_sources(app PRIVATE src/conformance_client.c "${repository}/tests/ssh/conformance.c")
    target_include_directories(app PRIVATE "${repository}/tests/ssh")
endif()
