get_filename_component(CRUX_EXECUTION_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(crux_execution_api INTERFACE)
target_include_directories(crux_execution_api INTERFACE
    "${CRUX_EXECUTION_COMPONENT_DIR}/include")
target_link_libraries(crux_execution_api INTERFACE crux::base)
# Portable translation units still need the selected CPU, instruction set,
# floating-point ABI and sysroot. These toolchain flags carry no kernel
# headers, generated configuration or native API declarations.
target_compile_options(crux_execution_api INTERFACE ${TOOLCHAIN_C_FLAGS})

add_library(crux_execution_zephyr STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../../src/execution_threads.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../src/execution_sync.c"
    "${CMAKE_CURRENT_LIST_DIR}/execution_native.c")
target_include_directories(crux_execution_zephyr PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src"
    "${CMAKE_CURRENT_LIST_DIR}")
# zephyr_library_named propagates zephyr_interface PUBLIC. Register this
# capability provider explicitly so native compile requirements stay PRIVATE.
zephyr_append_cmake_library(crux_execution_zephyr)
target_link_libraries(crux_execution_zephyr PRIVATE crux_execution_api zephyr_interface)
target_compile_options(crux_execution_zephyr PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(crux_execution_zephyr PRIVATE
    CRUX_EXECUTION_THREAD_COUNT=${CONFIG_CRUX_EXECUTION_THREAD_COUNT}
    CRUX_EXECUTION_MUTEX_COUNT=${CONFIG_CRUX_EXECUTION_MUTEX_COUNT}
    CRUX_EXECUTION_SEMAPHORE_COUNT=${CONFIG_CRUX_EXECUTION_SEMAPHORE_COUNT}
    CRUX_EXECUTION_STACK_BYTES=${CONFIG_CRUX_EXECUTION_STACK_BYTES})
set_property(TARGET crux_execution_zephyr PROPERTY INTERFACE_CRUX_EXECUTION_PROVIDER zephyr)
set_property(TARGET crux_execution_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_EXECUTION_PROVIDER)
add_library(crux_execution INTERFACE)
add_library(crux::execution ALIAS crux_execution)
# Native provider is already registered independently above.
target_link_libraries(crux_execution INTERFACE crux_execution_api)
