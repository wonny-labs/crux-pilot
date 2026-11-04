# Qualification is the revision-B EVKB composition, not the whole board family.
if(CONFIG_BOARD_MIMXRT1060_EVK AND NOT BOARD_REVISION STREQUAL "B")
    message(FATAL_ERROR "Crux Logging unsupported EVKB board revision: ${BOARD_REVISION}; expected B")
endif()
set(CRUX_LOGGING_PROVIDER zephyr CACHE STRING "Logging provider for this composition")
if(NOT CRUX_LOGGING_PROVIDER STREQUAL "zephyr")
    message(FATAL_ERROR "Crux Logging unsupported provider: ${CRUX_LOGGING_PROVIDER}")
endif()

foreach(required LOG LOG_MODE_DEFERRED LOG_PROCESS_THREAD)
    if(NOT CONFIG_${required})
        message(FATAL_ERROR "Crux Logging requires CONFIG_${required}=y")
    endif()
endforeach()

if(NOT CONFIG_LOG_BACKEND_UART AND NOT CONFIG_LOG_BACKEND_NET)
    if(CONFIG_NET_UDP AND NOT CONFIG_NET_SOCKETS)
        message(FATAL_ERROR "Crux Logging UDP requires CONFIG_NET_SOCKETS=y")
    elseif(CONFIG_NETWORKING AND CONFIG_NET_SOCKETS AND CONFIG_NET_UDP)
        message(FATAL_ERROR "Crux Logging UDP requires CONFIG_LOG_BACKEND_NET=y")
    else()
        message(FATAL_ERROR "Crux Logging requires at least one supported backend: CONFIG_LOG_BACKEND_UART or CONFIG_LOG_BACKEND_NET")
    endif()
endif()
if(CONFIG_LOG_BACKEND_UART)
    foreach(required LOG_BACKEND_UART_AUTOSTART LOG_BACKEND_UART_OUTPUT_TEXT LOG_BACKEND_SHOW_LEVEL)
        if(NOT CONFIG_${required})
            message(FATAL_ERROR "Crux Logging UART requires CONFIG_${required}=y")
        endif()
    endforeach()
endif()
if(CONFIG_LOG_BACKEND_NET)
    foreach(required NETWORKING NET_UDP NET_SOCKETS NET_CONNECTION_MANAGER
                     LOG_BACKEND_NET_AUTOSTART LOG_BACKEND_NET_USE_CONNECTION_MANAGER
                     LOG_BACKEND_NET_OUTPUT_TEXT LOG_BACKEND_SHOW_LEVEL)
        if(NOT CONFIG_${required})
            message(FATAL_ERROR "Crux Logging UDP requires CONFIG_${required}=y")
        endif()
    endforeach()
    if(CONFIG_LOG_BACKEND_NET_USE_DHCPV4_OPTION)
        message(FATAL_ERROR "Crux Logging UDP requires a fixed CONFIG_LOG_BACKEND_NET_SERVER address")
    endif()
    set(syslog_ipv4_enabled 0)
    set(syslog_ipv6_enabled 0)
    if(CONFIG_NET_IPV4)
        set(syslog_ipv4_enabled 1)
    endif()
    if(CONFIG_NET_IPV6)
        set(syslog_ipv6_enabled 1)
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/validate_syslog_server.py"
                "${CONFIG_LOG_BACKEND_NET_SERVER}" "${syslog_ipv4_enabled}" "${syslog_ipv6_enabled}"
        RESULT_VARIABLE server_address_result
        OUTPUT_VARIABLE server_address_output
        ERROR_VARIABLE server_address_error
    )
    if(NOT server_address_result EQUAL 0)
        message(FATAL_ERROR "Crux Logging UDP ${server_address_output}${server_address_error}")
    endif()
endif()

foreach(forbidden LOG_CUSTOM_HEADER LOG_BACKEND_CRLF_NONE SHELL_LOG_BACKEND LOG_MODE_OVERFLOW LOG_BLOCK_IN_THREAD LOG_RUNTIME_FILTERING LOG_FRONTEND
                  LOG_BACKEND_UART_ASYNC LOG_FMT_SECTION_STRIP
                  LOG_BACKEND_ADSP LOG_BACKEND_ADSP_MTRACE LOG_BACKEND_BLE
                  LOG_BACKEND_EFI_CONSOLE LOG_BACKEND_FS LOG_BACKEND_IPC_SERVICE
                  LOG_BACKEND_MQTT LOG_BACKEND_NATIVE_POSIX LOG_BACKEND_RTT
                  LOG_BACKEND_SEMIHOST LOG_BACKEND_SPINEL LOG_BACKEND_SWO LOG_BACKEND_WS
                  LOG_BACKEND_XTENSA_SIM)
    if(CONFIG_${forbidden})
        message(FATAL_ERROR "Crux Logging unsupported CONFIG_${forbidden}=y")
    endif()
endforeach()

if(CONFIG_LOG_MAX_LEVEL LESS 4 OR CONFIG_LOG_OVERRIDE_LEVEL GREATER 0)
    message(FATAL_ERROR "Crux Logging requires DEBUG native ceiling and no native override filter")
endif()

get_filename_component(CRUX_LOGGING_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(crux_logging_api INTERFACE)
target_include_directories(crux_logging_api INTERFACE "${CRUX_LOGGING_COMPONENT_DIR}/include")
target_link_libraries(crux_logging_api INTERFACE crux::base)
target_compile_options(crux_logging_api INTERFACE ${TOOLCHAIN_C_FLAGS})
add_library(crux_logging_zephyr STATIC ../../src/logging.c logging_zephyr.c logging_binding.c)
target_include_directories(crux_logging_zephyr PRIVATE ../../src .)
target_link_libraries(crux_logging_zephyr PRIVATE crux_logging_api zephyr_interface)
zephyr_append_cmake_library(crux_logging_zephyr)
target_compile_options(crux_logging_zephyr PRIVATE -Wall -Wextra -Werror)

set_property(TARGET crux_logging_zephyr PROPERTY INTERFACE_CRUX_LOGGING_PROVIDER zephyr)
set_property(TARGET crux_logging_zephyr APPEND PROPERTY COMPATIBLE_INTERFACE_STRING CRUX_LOGGING_PROVIDER)
add_library(crux_logging INTERFACE)
add_library(crux::logging ALIAS crux_logging)
target_link_libraries(crux_logging INTERFACE crux_logging_api)
cmake_language(DEFER CALL crux_assert_portable_usage crux::logging)
