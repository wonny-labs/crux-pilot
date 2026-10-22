cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_portable_execution LANGUAGES C)
add_library(crux_execution_cases STATIC ../conformance.c ../public_probe.c
    ../../execution_firmware/src/consumer.c)
set_target_properties(crux_execution_cases PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_link_libraries(crux_execution_cases PRIVATE crux::execution)
target_compile_options(crux_execution_cases PRIVATE -Wall -Wextra -Werror)
target_sources(app PRIVATE main.c)
target_sources(app PRIVATE ../admission.c)
target_include_directories(app PRIVATE ../../../components/execution/src
    ../../../components/execution/zephyr)
# app is a static library; propagate test-only wrappers to the final image link.
target_link_options(app PUBLIC -Wl,--wrap=exec_native_thread_join
    -Wl,--wrap=exec_native_mutex_lock -Wl,--wrap=exec_native_semaphore_take)
target_link_libraries(app PRIVATE crux_execution_cases crux::execution)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
