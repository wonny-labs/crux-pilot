# Memory is controlled development/test realtime; Linux uses CLOCK_REALTIME.
set(_crux_time_default "")
if(PROJECT_IS_TOP_LEVEL)
    set(_crux_time_default memory)
endif()
set(CRUX_TIME_PROVIDER "${_crux_time_default}" CACHE STRING "Time realtime provider: memory (development/test) or linux (system clock)")
set_property(CACHE CRUX_TIME_PROVIDER PROPERTY STRINGS memory linux)
if(NOT CRUX_TIME_PROVIDER MATCHES "^(memory|linux)$")
    message(FATAL_ERROR "Select CRUX_TIME_PROVIDER=memory or linux explicitly; other providers are unsupported")
endif()
message(STATUS "Crux Time realtime provider: ${CRUX_TIME_PROVIDER}")
find_package(Threads REQUIRED)

include(sources.cmake)

add_library(crux_time_common STATIC ${CRUX_TIME_COMMON_SOURCES})
target_include_directories(crux_time_common
    PUBLIC include
    PRIVATE src
)
target_link_libraries(crux_time_common PUBLIC crux::base)
target_compile_options(crux_time_common PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(CRUX_TIME_PROVIDER STREQUAL "memory")
    add_library(crux_time_memory STATIC src/time_port_memory.c)
    target_include_directories(crux_time_memory PRIVATE src include)
    target_link_libraries(crux_time_memory PRIVATE crux::base)
    target_compile_options(crux_time_memory PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_property(TARGET crux_time_memory PROPERTY INTERFACE_CRUX_TIME_BACKEND memory)
    set_property(TARGET crux_time_memory APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)

    # Internal compatibility name; the host composition explicitly selects memory.
    add_library(crux_time_host ALIAS crux_time_memory)
endif()

add_library(crux_time_clock_linux STATIC ports/linux/time_clock_linux.c)
target_include_directories(crux_time_clock_linux PRIVATE include src)
target_link_libraries(crux_time_clock_linux PRIVATE crux::base)
target_compile_definitions(crux_time_clock_linux PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_time_clock_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)

if(CRUX_TIME_PROVIDER STREQUAL "linux")
    add_library(crux_time_linux STATIC ports/linux/time_realtime_linux.c)
    target_include_directories(crux_time_linux PRIVATE include src)
    target_link_libraries(crux_time_linux PRIVATE crux::base)
    target_compile_definitions(crux_time_linux PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_options(crux_time_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)
    set_property(TARGET crux_time_linux PROPERTY INTERFACE_CRUX_TIME_BACKEND linux)
    set_property(TARGET crux_time_linux APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)
endif()

add_library(crux_time_lock_linux STATIC ports/linux/time_lock_linux.c)
target_include_directories(crux_time_lock_linux PRIVATE include src)
target_link_libraries(crux_time_lock_linux PRIVATE crux::base)
target_compile_definitions(crux_time_lock_linux PRIVATE _POSIX_C_SOURCE=200809L)
# Match Thread/Sync: supported glibc resolves pthread in libc; retain any real
# native link requirement on other toolchains for the boundary check to assess.
if(NOT CMAKE_HAVE_LIBC_PTHREAD)
    target_link_libraries(crux_time_lock_linux PRIVATE Threads::Threads)
endif()
target_compile_options(crux_time_lock_linux PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_time INTERFACE)
add_library(crux::time ALIAS crux_time)
target_link_libraries(crux_time INTERFACE crux_time_common crux_time_${CRUX_TIME_PROVIDER} crux_time_clock_linux crux_time_lock_linux)

target_include_directories(crux_time_common PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../interrupt/src")
target_link_libraries(crux_time_common PRIVATE crux_interrupt)
target_include_directories(crux_time_lock_linux PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../interrupt/src")
target_link_libraries(crux_time_lock_linux PRIVATE crux_interrupt)

option(CRUX_TIME_BUSY_WAIT "Enable native Time CPU busy-wait" ${PROJECT_IS_TOP_LEVEL})
if(CRUX_TIME_BUSY_WAIT)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Enabled CRUX_TIME_BUSY_WAIT requires a supported Linux provider")
    endif()
    add_library(crux_time_busy_wait STATIC ports/linux/busy_wait_linux.c)
    target_compile_definitions(crux_time_busy_wait PRIVATE _POSIX_C_SOURCE=200809L)
else()
    add_library(crux_time_busy_wait STATIC src/busy_wait_disabled.c)
endif()
target_include_directories(crux_time_busy_wait PRIVATE src)
target_link_libraries(crux_time_busy_wait PRIVATE crux::base)
target_compile_options(crux_time_busy_wait PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(crux_time INTERFACE crux_time_busy_wait)
