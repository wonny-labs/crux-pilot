# A qualified standard-interface usage target, intentionally no wrapper code.
add_library(crux_sockets INTERFACE)
add_library(crux::sockets ALIAS crux_sockets)
target_compile_definitions(crux_sockets INTERFACE _POSIX_C_SOURCE=200809L)
