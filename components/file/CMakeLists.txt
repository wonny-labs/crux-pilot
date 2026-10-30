add_library(crux_file_api INTERFACE)
target_include_directories(crux_file_api INTERFACE include)

add_library(crux_file_linux STATIC src/file.c ports/linux/file_linux.c ports/linux/file_lock.c)
find_package(Threads REQUIRED)
target_link_libraries(crux_file_linux PRIVATE crux_file_api)

# Match the supported Sync Host composition: libc supplies pthread on Ubuntu.
if(NOT CMAKE_HAVE_LIBC_PTHREAD)
    target_link_libraries(crux_file_linux PRIVATE Threads::Threads)
endif()
target_include_directories(crux_file_linux PRIVATE src ports/linux)
target_compile_definitions(crux_file_linux PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_file_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_file INTERFACE)
add_library(crux::file ALIAS crux_file)
target_link_libraries(crux_file INTERFACE crux_file_api crux_file_linux)
