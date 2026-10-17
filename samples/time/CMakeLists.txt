# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(crux_time_sample)

if(NOT CONFIG_CRUX_TIME)
    message(FATAL_ERROR "The Crux Time sample requires CONFIG_CRUX_TIME=y")
endif()

target_sources(app PRIVATE src/main.c src/time_shell.c src/time_command.c)
target_include_directories(app PRIVATE include)
target_link_libraries(app PRIVATE crux_time_common crux_time_zephyr)
