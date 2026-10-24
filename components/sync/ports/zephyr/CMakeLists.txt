get_filename_component(CRUX_SYNC_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(crux_sync_api INTERFACE)
target_include_directories(crux_sync_api INTERFACE
    "${CRUX_SYNC_COMPONENT_DIR}/include")
target_link_libraries(crux_sync_api INTERFACE crux::base)
# Portable translation units still need the selected CPU, instruction set,
# floating-point ABI and sysroot. These toolchain flags carry no kernel
# headers, generated configuration or native API declarations.
target_compile_options(crux_sync_api INTERFACE ${TOOLCHAIN_C_FLAGS})

add_library(crux_sync_zephyr STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../../src/sync.c"
    "${CMAKE_CURRENT_LIST_DIR}/sync_native.c")
target_include_directories(crux_sync_zephyr PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../src"
    "${CMAKE_CURRENT_LIST_DIR}")
# zephyr_library_named propagates zephyr_interface PUBLIC. Register this
# capability provider explicitly so native compile requirements stay PRIVATE.
zephyr_append_cmake_library(crux_sync_zephyr)
target_link_libraries(crux_sync_zephyr PRIVATE crux_sync_api zephyr_interface)
target_compile_options(crux_sync_zephyr PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(crux_sync_zephyr PRIVATE
    CRUX_SYNC_MUTEX_COUNT=${CONFIG_CRUX_SYNC_MUTEX_COUNT}
    CRUX_SYNC_SEMAPHORE_COUNT=${CONFIG_CRUX_SYNC_SEMAPHORE_COUNT})
set_property(TARGET crux_sync_zephyr PROPERTY INTERFACE_CRUX_SYNC_PROVIDER zephyr)
set_property(TARGET crux_sync_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_SYNC_PROVIDER)
add_library(crux_sync INTERFACE)
add_library(crux::sync ALIAS crux_sync)
# Native provider is already registered independently above.
target_link_libraries(crux_sync INTERFACE crux_sync_api)
