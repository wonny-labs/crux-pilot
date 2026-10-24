add_library(crux_queue INTERFACE)
add_library(crux::queue ALIAS crux_queue)
target_include_directories(crux_queue INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../../include")
target_link_libraries(crux_queue INTERFACE crux::base)
target_compile_options(crux_queue INTERFACE ${TOOLCHAIN_C_FLAGS})
add_library(crux_queue_zephyr STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../../src/queue.c"
    "${CMAKE_CURRENT_LIST_DIR}/queue_port.c")
target_include_directories(crux_queue_zephyr PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(crux_queue_zephyr PRIVATE crux::queue zephyr_interface)
zephyr_append_cmake_library(crux_queue_zephyr)
add_dependencies(crux_queue_zephyr zephyr_generated_headers)
target_compile_options(crux_queue_zephyr PRIVATE -Wall -Wextra -Werror)
