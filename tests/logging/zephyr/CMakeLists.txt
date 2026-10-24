cmake_minimum_required(VERSION 3.28)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_logging_tests LANGUAGES C CXX)
target_sources(app PRIVATE src/main.c)
target_link_libraries(app PRIVATE crux::logging)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

include(consumers.cmake)
