cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_networking_integration LANGUAGES C)
target_sources(app PRIVATE src/main.c ../networking/stack_scenario.c ../networking/runtime_port.c
    ../../samples/networking/client.c)
target_include_directories(app PRIVATE ../networking ../../samples/networking)
target_link_libraries(app PRIVATE crux::sockets crux::netif)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
