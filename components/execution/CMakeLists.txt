add_library(crux_execution_api INTERFACE)
target_include_directories(crux_execution_api INTERFACE include)
target_link_libraries(crux_execution_api INTERFACE crux::base)

find_package(Threads REQUIRED)
add_library(crux_execution_host STATIC src/execution_threads.c src/execution_sync.c
    host/execution_native.c host/execution_sync_native.c)
target_link_libraries(crux_execution_host PRIVATE crux_execution_api Threads::Threads)
target_include_directories(crux_execution_host PRIVATE src host)
target_compile_definitions(crux_execution_host PRIVATE _GNU_SOURCE)
target_compile_options(crux_execution_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_execution_host PROPERTY INTERFACE_CRUX_EXECUTION_PROVIDER host)
set_property(TARGET crux_execution_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_EXECUTION_PROVIDER)

add_library(crux_execution INTERFACE)
add_library(crux::execution ALIAS crux_execution)
target_link_libraries(crux_execution INTERFACE crux_execution_api crux_execution_host)
