message(STATUS "Crux Zephyr integration module discovered")

if(CONFIG_CRUX_PILOT_MODULE)
    add_library(crux_base INTERFACE)
    target_include_directories(crux_base INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/base/include"
    )
    add_library(crux::base ALIAS crux_base)
endif()

if(CONFIG_CRUX_EXECUTION)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/execution/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/execution")
endif()

if(CONFIG_CRUX_TIME)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/time/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/time")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/CruxConsumerBoundary.cmake")
if(CONFIG_CRUX_PILOT_MODULE)
    cmake_language(DEFER CALL crux_assert_portable_usage crux::base)
endif()
if(CONFIG_CRUX_TIME)
    cmake_language(DEFER CALL crux_assert_portable_usage crux::time)
endif()

if(CONFIG_CRUX_SOCKETS)
    add_library(crux_sockets INTERFACE)
    add_library(crux::sockets ALIAS crux_sockets)
    target_compile_definitions(crux_sockets INTERFACE _POSIX_C_SOURCE=200809L)
    target_compile_options(crux_sockets INTERFACE
        "SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/../components/sockets/zephyr/close_declaration.h")
endif()
if(CONFIG_CRUX_NETIF)
    add_library(crux_netif INTERFACE)
    target_include_directories(crux_netif INTERFACE ../components/netif/include)
    target_link_libraries(crux_netif INTERFACE crux::base)
    add_library(crux::netif ALIAS crux_netif)
    # Native final-image registration owns provider linkage, independently of
    # the portable usage facade (STATIC PRIVATE would export LINK_ONLY).
    zephyr_library_named(crux_netif_provider)
    zephyr_library_sources(../components/netif/src/netif.c
        ../components/netif/zephyr/netif_zephyr.c)
    zephyr_library_include_directories(../components/netif/include ../components/netif/src)
    zephyr_library_link_libraries(crux::base)
    add_dependencies(crux_netif_provider zephyr_generated_headers)
    target_compile_options(crux_netif_provider PRIVATE -Wall -Wextra -Werror)
    cmake_language(DEFER CALL crux_assert_portable_usage crux::netif)
endif()
if(CONFIG_CRUX_SOCKETS)
    cmake_language(DEFER CALL crux_assert_portable_usage crux::sockets)
endif()

if(CONFIG_CRUX_QUEUE)
    add_library(crux_queue INTERFACE)
    add_library(crux::queue ALIAS crux_queue)
    target_include_directories(crux_queue INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/queue/include")
    target_link_libraries(crux_queue INTERFACE crux::base)
    target_compile_options(crux_queue INTERFACE ${TOOLCHAIN_C_FLAGS})
    add_library(crux_queue_zephyr STATIC
        "${CMAKE_CURRENT_LIST_DIR}/../components/queue/src/queue.c"
        "${CMAKE_CURRENT_LIST_DIR}/../components/queue/zephyr/queue_port.c")
    target_include_directories(crux_queue_zephyr PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../components/queue/zephyr")
    target_link_libraries(crux_queue_zephyr PRIVATE crux::queue zephyr_interface)
    zephyr_append_cmake_library(crux_queue_zephyr)
    add_dependencies(crux_queue_zephyr zephyr_generated_headers)
    target_compile_options(crux_queue_zephyr PRIVATE -Wall -Wextra -Werror)
endif()
if(CONFIG_CRUX_MONOTONIC)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/monotonic/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/monotonic")
endif()
