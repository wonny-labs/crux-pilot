# SFTP on native_sim: the wolfSSH provider of #192, the real entry points and access adapter, the real file
# service, and File on real FatFs over a RAM disk, for OpenSSH on the host to reach. Test only: the Access
# ports and the host key are the test ones of tests/zephyr/ssh_provider. scripts/test-ssh-sftp-native.sh
# drives it, and it refuses to start until the provider exists.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(repository "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
list(APPEND ZEPHYR_EXTRA_MODULES "${repository}")
list(APPEND EXTRA_CONF_FILE "${repository}/components/ssh/ports/zephyr/wolf-ssh.conf"
    "${repository}/tests/zephyr/ssh_provider/native_sim.conf")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_sftp_native C)

set(CRUX_SSH_SFTP_FILE_SERVICE ON)
include("${repository}/components/ssh/ports/zephyr/wolf_ssh_settings.cmake")
crux_ssh_wolf_settings()
target_compile_definitions(crux_ssh_wolf_provider PRIVATE CRUX_SSH_WOLF_TESTING=1)

set(provider_test "${repository}/tests/zephyr/ssh_provider/src")
target_sources(app PRIVATE
    src/main.c
    "${provider_test}/test_access_ports.c"
    "${provider_test}/test_keys.c"
    "${repository}/components/file_service/src/file_service.c"
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
    "${repository}/components/file_service/include"
    "${repository}/components/file_service/src"
    "${repository}/components/file/ports/zephyr"
    "${repository}/components/ssh/adapter"
    "${repository}/components/ssh/ports/zephyr")
target_compile_definitions(app PRIVATE CRUX_SSH_WOLF_TESTING=1)
target_link_libraries(app PRIVATE crux::file crux::sync)
