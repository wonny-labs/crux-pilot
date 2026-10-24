add_library(crux_sockets INTERFACE)
add_library(crux::sockets ALIAS crux_sockets)
target_compile_definitions(crux_sockets INTERFACE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_sockets INTERFACE
    "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/close_declaration.h")
