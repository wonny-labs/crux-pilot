get_filename_component(CRUX_THREAD_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(crux_thread_api INTERFACE)
target_include_directories(crux_thread_api INTERFACE
    "${CRUX_THREAD_COMPONENT_DIR}/include")
target_link_libraries(crux_thread_api INTERFACE crux::base)
# Portable translation units still need the selected CPU, instruction set,
# floating-point ABI and sysroot. These toolchain flags carry no kernel
# headers, generated configuration or native API declarations.
target_compile_options(crux_thread_api INTERFACE ${TOOLCHAIN_C_FLAGS})

add_library(crux_thread_zephyr STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../../src/thread.c"
    "${CMAKE_CURRENT_LIST_DIR}/thread_native.c")
target_include_directories(crux_thread_zephyr PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src"
    "${CMAKE_CURRENT_LIST_DIR}")
# zephyr_library_named propagates zephyr_interface PUBLIC. Register this
# capability provider explicitly so native compile requirements stay PRIVATE.
zephyr_append_cmake_library(crux_thread_zephyr)
target_link_libraries(crux_thread_zephyr PRIVATE crux_thread_api zephyr_interface)
target_compile_options(crux_thread_zephyr PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(crux_thread_zephyr PRIVATE
    CRUX_THREAD_COUNT=${CONFIG_CRUX_THREAD_COUNT}
    CRUX_THREAD_STACK_BYTES=${CONFIG_CRUX_THREAD_STACK_BYTES})
set_property(TARGET crux_thread_zephyr PROPERTY INTERFACE_CRUX_THREAD_PROVIDER zephyr)
set_property(TARGET crux_thread_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_THREAD_PROVIDER)
add_library(crux_thread INTERFACE)
add_library(crux::thread ALIAS crux_thread)
# Native provider is already registered independently above.
target_link_libraries(crux_thread INTERFACE crux_thread_api)
