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
    # Native provider is already registered independently above.
    target_link_libraries(crux_execution INTERFACE crux_execution_api)
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
