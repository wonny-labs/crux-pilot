add_library(crux_sync_api INTERFACE)
target_include_directories(crux_sync_api INTERFACE include)
target_link_libraries(crux_sync_api INTERFACE crux::base)

find_package(Threads REQUIRED)
add_library(crux_sync_host STATIC src/sync.c ports/linux/sync_native.c)
target_link_libraries(crux_sync_host PRIVATE crux_sync_api)
# Supported Ubuntu/glibc Host resolves pthread symbols from libc. An empty
# Threads target must not become a transitive native consumer requirement.
# Preserve the native link requirement on other toolchains rather than hiding it.
if(NOT CMAKE_HAVE_LIBC_PTHREAD)
    target_link_libraries(crux_sync_host PRIVATE Threads::Threads)
endif()
target_include_directories(crux_sync_host PRIVATE src ports/linux)
target_compile_definitions(crux_sync_host PRIVATE _GNU_SOURCE)
target_compile_options(crux_sync_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_sync_host PROPERTY INTERFACE_CRUX_SYNC_PROVIDER host)
set_property(TARGET crux_sync_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_SYNC_PROVIDER)

add_library(crux_sync INTERFACE)
add_library(crux::sync ALIAS crux_sync)
target_link_libraries(crux_sync INTERFACE crux_sync_api crux_sync_host)
