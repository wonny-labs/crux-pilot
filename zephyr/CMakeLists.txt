message(STATUS "Crux Zephyr integration module discovered")

if(CONFIG_CRUX_PILOT_MODULE)
    add_library(crux_base INTERFACE)
    target_include_directories(crux_base INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/../components/base/include"
    )
    add_library(crux::base ALIAS crux_base)
endif()

if(CONFIG_CRUX_THREAD)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/thread/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/thread")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::thread)
endif()
if(CONFIG_CRUX_SYNC)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/sync/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/sync")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::sync)
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
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/sockets/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/sockets")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::sockets)
endif()
if(CONFIG_CRUX_NETIF)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/netif/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/netif")
    cmake_language(DEFER CALL crux_assert_portable_usage crux::netif)
endif()

if(CONFIG_CRUX_QUEUE)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/queue/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/queue")
endif()
if(CONFIG_CRUX_MONOTONIC)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/monotonic/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/monotonic")
endif()

if(CONFIG_CRUX_SERIAL)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../components/serial/ports/zephyr"
        "${CMAKE_CURRENT_BINARY_DIR}/serial")
endif()
