if(NOT TARGET crux::time)
    message(FATAL_ERROR "Delay requires CRUX_BUILD_TIME=ON and an explicit supported Time provider for typed values")
endif()

add_library(crux_delay_api INTERFACE)
target_include_directories(crux_delay_api INTERFACE include)
target_link_libraries(crux_delay_api INTERFACE crux::time)

add_library(crux_delay_linux STATIC src/delay.c ports/linux/delay_linux.c)
target_link_libraries(crux_delay_linux PRIVATE crux_delay_api)
target_include_directories(crux_delay_linux PRIVATE src)
target_compile_definitions(crux_delay_linux PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_delay_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_delay INTERFACE)
add_library(crux::delay ALIAS crux_delay)
target_link_libraries(crux_delay INTERFACE crux_delay_api crux_delay_linux)
