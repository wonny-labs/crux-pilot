add_library(crux_monotonic INTERFACE)
add_library(crux::monotonic ALIAS crux_monotonic)
target_include_directories(crux_monotonic INTERFACE include)
target_link_libraries(crux_monotonic INTERFACE crux::base)

# Native provider belongs to final composition, never the public usage graph.
add_library(crux_monotonic_host STATIC src/monotonic.c ports/linux/monotonic_host.c)
target_include_directories(crux_monotonic_host PRIVATE src)
target_link_libraries(crux_monotonic_host PRIVATE crux::monotonic)
target_compile_definitions(crux_monotonic_host PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_monotonic_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_monotonic_host PROPERTY INTERFACE_CRUX_MONOTONIC_PROVIDER host)
set_property(TARGET crux_monotonic_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_MONOTONIC_PROVIDER)

# Supported native final-composition target; never link it into portable source targets.
add_library(crux::monotonic_host ALIAS crux_monotonic_host)
