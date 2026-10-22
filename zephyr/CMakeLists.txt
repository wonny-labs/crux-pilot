message(STATUS "Crux Zephyr integration module discovered")

if(CONFIG_CRUX_PILOT_MODULE)
    add_library(crux_base INTERFACE)
    target_include_directories(crux_base INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/base/include"
    )
    add_library(crux::base ALIAS crux_base)
endif()

if(CONFIG_CRUX_EXECUTION)
    add_library(crux_execution_api INTERFACE)
    target_include_directories(crux_execution_api INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/execution/include")
    target_link_libraries(crux_execution_api INTERFACE crux::base)
    # Portable translation units still need the selected CPU, instruction set,
    # floating-point ABI and sysroot. These toolchain flags carry no kernel
    # headers, generated configuration or native API declarations.
    target_compile_options(crux_execution_api INTERFACE ${TOOLCHAIN_C_FLAGS})

    add_library(crux_execution_zephyr STATIC
        "${CMAKE_CURRENT_LIST_DIR}/../components/execution/src/execution_threads.c"
        "${CMAKE_CURRENT_LIST_DIR}/../components/execution/src/execution_sync.c"
        "${CMAKE_CURRENT_LIST_DIR}/../components/execution/zephyr/execution_native.c")
    target_include_directories(crux_execution_zephyr PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../components/execution/src"
        "${CMAKE_CURRENT_LIST_DIR}/../components/execution/zephyr")
    # zephyr_library_named propagates zephyr_interface PUBLIC. Register this
    # capability provider explicitly so native compile requirements stay PRIVATE.
    zephyr_append_cmake_library(crux_execution_zephyr)
    target_link_libraries(crux_execution_zephyr PRIVATE crux_execution_api zephyr_interface)
    target_compile_options(crux_execution_zephyr PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(crux_execution_zephyr PRIVATE
        CRUX_EXECUTION_THREAD_COUNT=${CONFIG_CRUX_EXECUTION_THREAD_COUNT}
        CRUX_EXECUTION_MUTEX_COUNT=${CONFIG_CRUX_EXECUTION_MUTEX_COUNT}
        CRUX_EXECUTION_SEMAPHORE_COUNT=${CONFIG_CRUX_EXECUTION_SEMAPHORE_COUNT}
        CRUX_EXECUTION_STACK_BYTES=${CONFIG_CRUX_EXECUTION_STACK_BYTES})
    set_property(TARGET crux_execution_zephyr PROPERTY INTERFACE_CRUX_EXECUTION_PROVIDER zephyr)
    set_property(TARGET crux_execution_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_EXECUTION_PROVIDER)
    add_library(crux_execution INTERFACE)
    add_library(crux::execution ALIAS crux_execution)
    target_link_libraries(crux_execution INTERFACE crux_execution_api crux_execution_zephyr)
endif()

if(CONFIG_CRUX_TIME)
    add_library(crux_time_api INTERFACE)
    target_include_directories(crux_time_api INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/time/include"
    )
    target_link_libraries(crux_time_api INTERFACE crux_base zephyr_interface)

    include("${CMAKE_CURRENT_LIST_DIR}/../components/time/sources.cmake")
    zephyr_library_named(crux_time_common)
    zephyr_library_sources(${CRUX_TIME_COMMON_SOURCES})
    zephyr_library_include_directories(
        "${CMAKE_CURRENT_LIST_DIR}/../components/time/include"
        "${CMAKE_CURRENT_LIST_DIR}/../components/time/src"
    )
    zephyr_library_link_libraries(crux_time_api)
    target_compile_options(crux_time_common PRIVATE -Wall -Wextra -Werror)
    zephyr_library_compile_definitions_ifdef(CONFIG_CRUX_TIME_TESTING CRUX_TIME_TESTING=1)

    zephyr_library_named(crux_time_zephyr)
    zephyr_library_sources(
        "${CMAKE_CURRENT_LIST_DIR}/../components/time/zephyr/time_port_zephyr.c"
    )
    zephyr_library_include_directories(
        "${CMAKE_CURRENT_LIST_DIR}/../components/base/include"
        "${CMAKE_CURRENT_LIST_DIR}/../components/time/include"
        "${CMAKE_CURRENT_LIST_DIR}/../components/time/src"
    )
    zephyr_library_link_libraries(crux_time_api)
    target_compile_options(crux_time_zephyr PRIVATE -Wall -Wextra -Werror)
    set_property(TARGET crux_time_zephyr PROPERTY INTERFACE_CRUX_TIME_BACKEND zephyr_rtc)
    set_property(TARGET crux_time_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND)

    add_library(crux_time INTERFACE)
    add_library(crux::time ALIAS crux_time)
    target_link_libraries(crux_time INTERFACE
        crux_time_api crux_time_common crux_time_zephyr
    )
endif()

if(CONFIG_CRUX_SOCKETS)
    add_library(crux_sockets INTERFACE)
    add_library(crux::sockets ALIAS crux_sockets)
    target_compile_definitions(crux_sockets INTERFACE _POSIX_C_SOURCE=200809L)
    target_compile_options(crux_sockets INTERFACE
        "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/../components/sockets/zephyr/close_declaration.h")
endif()
if(CONFIG_CRUX_NETIF)
    add_library(crux_netif STATIC ../components/netif/src/netif.c
        ../components/netif/zephyr/netif_zephyr.c)
    target_include_directories(crux_netif PUBLIC ../components/netif/include
        PRIVATE ../components/netif/src)
    target_link_libraries(crux_netif PUBLIC crux::base PRIVATE zephyr_interface)
    add_dependencies(crux_netif zephyr_generated_headers)
    target_compile_options(crux_netif PRIVATE -Wall -Wextra -Werror)
    add_library(crux::netif ALIAS crux_netif)
endif()
