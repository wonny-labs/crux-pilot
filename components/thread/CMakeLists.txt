add_library(crux_thread_api INTERFACE)
target_include_directories(crux_thread_api INTERFACE include)
target_link_libraries(crux_thread_api INTERFACE crux::base)

find_package(Threads REQUIRED)
add_library(crux_thread_host STATIC src/thread.c ports/linux/thread_native.c)
target_link_libraries(crux_thread_host PRIVATE crux_thread_api)
# Supported Ubuntu/glibc Host resolves pthread symbols from libc. An empty
# Threads target must not become a transitive native consumer requirement.
# Preserve the native link requirement on other toolchains rather than hiding it.
if(NOT CMAKE_HAVE_LIBC_PTHREAD)
    target_link_libraries(crux_thread_host PRIVATE Threads::Threads)
endif()
target_include_directories(crux_thread_host PRIVATE src ports/linux)
target_compile_definitions(crux_thread_host PRIVATE _GNU_SOURCE)
target_compile_options(crux_thread_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_thread_host PROPERTY INTERFACE_CRUX_THREAD_PROVIDER host)
set_property(TARGET crux_thread_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_THREAD_PROVIDER)

add_library(crux_thread INTERFACE)
add_library(crux::thread ALIAS crux_thread)
target_link_libraries(crux_thread INTERFACE crux_thread_api crux_thread_host)
