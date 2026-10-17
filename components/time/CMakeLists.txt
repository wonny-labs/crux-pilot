include(sources.cmake)

add_library(crux_time_common STATIC ${CRUX_TIME_COMMON_SOURCES})
target_include_directories(crux_time_common
    PUBLIC include
    PRIVATE src
)
target_link_libraries(crux_time_common PUBLIC crux::base)
target_compile_options(crux_time_common PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_time_host STATIC host/time_port_host.c)
target_include_directories(crux_time_host PRIVATE src include)
target_link_libraries(crux_time_host PRIVATE crux::base)
target_compile_options(crux_time_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_time_host PROPERTY INTERFACE_CRUX_TIME_BACKEND host)
set_property(TARGET crux_time_host APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)

add_library(crux_time INTERFACE)
add_library(crux::time ALIAS crux_time)
target_link_libraries(crux_time INTERFACE crux_time_common crux_time_host)
