# The Host memory implementation is for controlled development/tests, not production Linux RTC.
set(_crux_time_default "")
if(PROJECT_IS_TOP_LEVEL)
    set(_crux_time_default memory)
endif()
set(CRUX_TIME_PROVIDER "${_crux_time_default}" CACHE STRING "Host Time provider (memory: development/test only)")
set_property(CACHE CRUX_TIME_PROVIDER PROPERTY STRINGS memory)
if(NOT CRUX_TIME_PROVIDER STREQUAL "memory")
    message(FATAL_ERROR "Select CRUX_TIME_PROVIDER=memory explicitly for Host development/test Time; other providers are unsupported")
endif()
message(STATUS "Crux Host Time uses the explicitly selected development/test memory provider")

include(sources.cmake)

add_library(crux_time_common STATIC ${CRUX_TIME_COMMON_SOURCES})
target_include_directories(crux_time_common
    PUBLIC include
    PRIVATE src
)
target_link_libraries(crux_time_common PUBLIC crux::base)
target_compile_options(crux_time_common PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_time_memory STATIC src/time_port_memory.c)
target_include_directories(crux_time_memory PRIVATE src include)
target_link_libraries(crux_time_memory PRIVATE crux::base)
target_compile_options(crux_time_memory PRIVATE -Wall -Wextra -Wpedantic -Werror)
set_property(TARGET crux_time_memory PROPERTY INTERFACE_CRUX_TIME_BACKEND memory)
set_property(TARGET crux_time_memory APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)

# Internal compatibility name; the host composition explicitly selects memory.
add_library(crux_time_host ALIAS crux_time_memory)

add_library(crux_time_clock_linux STATIC ports/linux/time_clock_linux.c)
target_include_directories(crux_time_clock_linux PRIVATE include src)
target_link_libraries(crux_time_clock_linux PRIVATE crux::base)
target_compile_definitions(crux_time_clock_linux PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_time_clock_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_time INTERFACE)
add_library(crux::time ALIAS crux_time)
target_link_libraries(crux_time INTERFACE crux_time_common crux_time_memory crux_time_clock_linux)
