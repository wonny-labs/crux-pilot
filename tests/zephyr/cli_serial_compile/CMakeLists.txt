cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_cli_serial_compile C)
# An unsatisfied Kconfig dependency only warns, which would build an empty application.
if(NOT CONFIG_CRUX_CLI)
    message(FATAL_ERROR "The command line compile check is not selected")
endif()
target_sources(app PRIVATE main.c)
