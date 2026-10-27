add_library(crux_queue INTERFACE)
add_library(crux::queue ALIAS crux_queue)
target_include_directories(crux_queue INTERFACE include)
target_link_libraries(crux_queue INTERFACE crux::base)
find_package(Threads REQUIRED)
add_library(crux_queue_linux STATIC src/queue.c ports/linux/queue_port.c)
target_link_libraries(crux_queue_linux PRIVATE crux::queue Threads::Threads)
target_include_directories(crux_queue_linux PRIVATE ports/linux)
target_compile_definitions(crux_queue_linux PRIVATE _GNU_SOURCE)
target_compile_options(crux_queue_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Supported native final-composition target; never link it into portable source targets.
add_library(crux::queue_linux ALIAS crux_queue_linux)

target_include_directories(crux_queue_linux PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../interrupt/src")
target_link_libraries(crux_queue_linux PRIVATE crux_interrupt)
