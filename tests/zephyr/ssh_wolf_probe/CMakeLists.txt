# Compile and link check of the wolfSSH settings arrangement. Never flashed or run.
cmake_minimum_required(VERSION 3.20.0)
list(APPEND EXTRA_CONF_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../../components/ssh/ports/zephyr/wolf-ssh.conf")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_ssh_wolf_probe)

include(../../../components/ssh/ports/zephyr/wolf_ssh_settings.cmake)
crux_ssh_wolf_settings()

# wolfSSH's sources belong to the app target, as its module adds them there. Crux code that includes
# wolfSSH headers goes in its own library, with the warning policy for owned code.
add_library(crux_ssh_probe_owned OBJECT src/owned.c)
target_link_libraries(crux_ssh_probe_owned PRIVATE zephyr_interface)
add_dependencies(crux_ssh_probe_owned zephyr_generated_headers)
crux_ssh_wolfssh_client(crux_ssh_probe_owned)
target_include_directories(crux_ssh_probe_owned PRIVATE port)
target_compile_options(crux_ssh_probe_owned PRIVATE -Wall -Wextra -Werror)

target_sources(app PRIVATE src/main.c $<TARGET_OBJECTS:crux_ssh_probe_owned>)
target_include_directories(app PRIVATE port)
