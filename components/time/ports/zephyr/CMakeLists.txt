get_filename_component(CRUX_TIME_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(crux_time_api INTERFACE)
target_include_directories(crux_time_api INTERFACE
    "${CRUX_TIME_COMPONENT_DIR}/include"
)
target_link_libraries(crux_time_api INTERFACE crux_base)

include("${CRUX_TIME_COMPONENT_DIR}/sources.cmake")
zephyr_library_named(crux_time_common)
zephyr_library_sources(${CRUX_TIME_COMMON_SOURCES})
zephyr_library_include_directories(
    "${CRUX_TIME_COMPONENT_DIR}/include"
    "${CRUX_TIME_COMPONENT_DIR}/src"
)
zephyr_library_link_libraries(crux_time_api)
target_compile_options(crux_time_common PRIVATE -Wall -Wextra -Werror)
zephyr_library_compile_definitions_ifdef(CONFIG_CRUX_TIME_TESTING CRUX_TIME_TESTING=1)

zephyr_library_named(crux_time_zephyr)
zephyr_library_sources(
    "${CRUX_TIME_COMPONENT_DIR}/ports/zephyr/time_port_zephyr.c"
    "${CRUX_TIME_COMPONENT_DIR}/ports/zephyr/time_clock_zephyr.c"
)
zephyr_library_include_directories(
    "${CRUX_TIME_COMPONENT_DIR}/../base/include"
    "${CRUX_TIME_COMPONENT_DIR}/include"
    "${CRUX_TIME_COMPONENT_DIR}/src"
)
zephyr_library_link_libraries(crux_time_api)
target_compile_options(crux_time_zephyr PRIVATE -Wall -Wextra -Werror)
set_property(TARGET crux_time_zephyr PROPERTY INTERFACE_CRUX_TIME_BACKEND zephyr_rtc)
set_property(TARGET crux_time_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)

add_library(crux_time INTERFACE)
add_library(crux::time ALIAS crux_time)
# Zephyr registers both implementations in the final image independently.
# Consumers need only portable declarations, not native implementation usage.
target_link_libraries(crux_time INTERFACE crux_time_api)
set_property(TARGET crux_time PROPERTY INTERFACE_CRUX_TIME_BACKEND zephyr_rtc)
set_property(TARGET crux_time APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)
