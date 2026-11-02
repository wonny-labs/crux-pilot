cmake_minimum_required(VERSION 3.20.0)
# The provider modules are supplied by the composition (ZEPHYR_MODULES from the caller), not by
# this repository. The cases need no Crux component.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_psa_conformance C)
target_sources(app PRIVATE src/main.c ../cases/psa_cases.c)
target_include_directories(app PRIVATE ../cases)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
