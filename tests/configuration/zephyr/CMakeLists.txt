cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_configuration_software LANGUAGES C)
target_sources(app PRIVATE main.c ../conformance.c ../fault_provider.c
    ../../../components/configuration/src/configuration.c)
target_include_directories(app PRIVATE .. ../../../components/configuration/include
    ../../../components/configuration/src)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
