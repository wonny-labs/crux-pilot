# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(crux_time_zephyr_tests)

get_filename_component(CRUX_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

target_sources(app PRIVATE
    src/main.c
    "${CRUX_REPO_ROOT}/samples/time/src/time_command.c"
    "${CRUX_REPO_ROOT}/samples/time/src/time_shell.c"
)
target_include_directories(app PRIVATE
    "${CRUX_REPO_ROOT}/components/time/src"
    "${CRUX_REPO_ROOT}/components/time/zephyr"
    "${CRUX_REPO_ROOT}/samples/time/include"
    "${CRUX_REPO_ROOT}/tests/time/private"
)
target_link_libraries(app PRIVATE crux::time)
