cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_sync_priority)
target_sources(app PRIVATE main.c)
target_include_directories(app PRIVATE ../../../components/sync/src
    ../../../components/sync/ports/zephyr)
# app is a static library; propagate the test-only wrappers to the final image link.
target_link_options(app PUBLIC -Wl,--wrap=crux_mutex_impl_unlock
    -Wl,--wrap=crux_semaphore_impl_create)
target_link_libraries(app PRIVATE crux::sync)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
