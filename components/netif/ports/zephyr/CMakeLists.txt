add_library(crux_netif INTERFACE)
target_include_directories(crux_netif INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/../../include")
target_link_libraries(crux_netif INTERFACE crux::base)
add_library(crux::netif ALIAS crux_netif)
# Native final-image registration owns provider linkage, independently of
# the portable usage facade (STATIC PRIVATE would export LINK_ONLY).
zephyr_library_named(crux_netif_provider)
zephyr_library_sources(
    "${CMAKE_CURRENT_LIST_DIR}/../../src/netif.c"
    "${CMAKE_CURRENT_LIST_DIR}/netif_zephyr.c")
zephyr_library_include_directories(
    "${CMAKE_CURRENT_LIST_DIR}/../../include"
    "${CMAKE_CURRENT_LIST_DIR}/../../src")
zephyr_library_link_libraries(crux::base)
add_dependencies(crux_netif_provider zephyr_generated_headers)
target_compile_options(crux_netif_provider PRIVATE -Wall -Wextra -Werror)
