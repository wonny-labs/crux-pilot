# Compile and link check of the wolfSSH settings arrangement. Never flashed or run.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND EXTRA_CONF_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/ssh/ports/zephyr/wolf-ssh.conf")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_wolf_probe)

include(../../../components/ssh/ports/zephyr/wolf_ssh_settings.cmake)
crux_ssh_wolf_settings()

target_sources(app PRIVATE src/main.c)
