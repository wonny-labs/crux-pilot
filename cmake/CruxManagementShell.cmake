include_guard(GLOBAL)

# Rule L5 of the command line contract, at configuration. zephyr/CMakeLists.txt calls this for every
# Crux Zephyr build, because no management component exists to check an image without management.
# It reads the CONFIG_ variables that Zephyr has imported. scripts/check-management-shell.py applies
# the same table to a built image.
function(crux_check_management_shell)
    foreach(symbol IN ITEMS SHELL_BACKEND_TELNET SHELL_BACKEND_WEBSOCKET SHELL_BACKEND_MQTT
                            MCUMGR_GRP_SHELL)
        if(CONFIG_${symbol})
            message(FATAL_ERROR "Crux images must not enable CONFIG_${symbol}: "
                                "the Zephyr shell never reaches the network")
        endif()
    endforeach()
    if(NOT (CONFIG_CRUX_CLI OR CONFIG_CRUX_WEB OR CONFIG_CRUX_SSH))
        return()
    endif()
    if(CONFIG_SHELL AND NOT CONFIG_CRUX_DEVELOPMENT_IMAGE)
        message(FATAL_ERROR "An image with Crux management must not enable CONFIG_SHELL unless "
                            "CONFIG_CRUX_DEVELOPMENT_IMAGE marks it")
    endif()
    # An allow list, so that RTT, RPMSG, the dummy backend and any later one are refused too. Zephyr
    # imports only the symbols that are set, so being defined is what counts here.
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
        if(NOT variable MATCHES "^CONFIG_SHELL_BACKEND_")
            continue()
        endif()
        if(NOT CONFIG_CRUX_DEVELOPMENT_IMAGE)
            message(FATAL_ERROR "An image with Crux management must not set ${variable} unless "
                                "CONFIG_CRUX_DEVELOPMENT_IMAGE marks it")
        endif()
        if(NOT variable MATCHES "^CONFIG_SHELL_BACKEND_SERIAL(_|$)")
            message(FATAL_ERROR "An image with Crux management may use only the serial shell "
                                "backend, and it sets ${variable}")
        endif()
    endforeach()
endfunction()
