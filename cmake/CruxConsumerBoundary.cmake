include_guard(GLOBAL)

# Check source-tree usage requirements, conservatively considering every branch
# of the supported generator expressions. This deliberately is not a general
# generator-expression interpreter: opaque expressions fail closed. Install-tree
# requirements need a separate installed-package consumer check.
function(_crux_consumer_values output value)
    while("${value}" MATCHES "\\$<([^$<>]*)>")
        set(expression "${CMAKE_MATCH_0}")
        set(body "${CMAKE_MATCH_1}")
        if(body MATCHES "^(BUILD_INTERFACE|BUILD_LOCAL_INTERFACE|LINK_ONLY|TARGET_NAME_IF_EXISTS):(.*)$")
            set(replacement "${CMAKE_MATCH_2}")
        elseif(body MATCHES "^INSTALL_INTERFACE:")
            set(replacement "")
        elseif(body MATCHES "^(BOOL|CONFIG|PLATFORM_ID|COMPILE_LANGUAGE|LINK_LANGUAGE|C_COMPILER_ID|CXX_COMPILER_ID|NOT|AND|OR|STREQUAL|EQUAL):")
            # Both conditional branches are inspected, regardless of configuration.
            set(replacement "1")
        elseif(body MATCHES "^[01]:(.*)$")
            set(replacement "${CMAKE_MATCH_1}")
        elseif(body MATCHES "^IF:[01],([^,]*),([^,]*)$")
            set(replacement "${CMAKE_MATCH_1};${CMAKE_MATCH_2}")
        else()
            message(FATAL_ERROR "unsupported consumer expression: ${expression}")
        endif()
        string(REPLACE "${expression}" "${replacement}" value "${value}")
    endwhile()
    if("${value}" MATCHES "\\$<")
        message(FATAL_ERROR "unsupported consumer expression: ${value}")
    endif()
    set(${output} "${value}" PARENT_SCOPE)
endfunction()

function(_crux_check_consumer_target target visited)
    if("${target}" IN_LIST visited)
        return()
    endif()
    list(APPEND visited "${target}")
    set(properties INTERFACE_INCLUDE_DIRECTORIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                   INTERFACE_LINK_LIBRARIES INTERFACE_LINK_LIBRARIES_DIRECT INTERFACE_LINK_DIRECTORIES
                   INTERFACE_COMPILE_DEFINITIONS INTERFACE_COMPILE_OPTIONS
                   INTERFACE_LINK_OPTIONS)
    if(ARGC GREATER 2)
        list(APPEND properties INCLUDE_DIRECTORIES LINK_LIBRARIES LINK_DIRECTORIES
                               COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_OPTIONS)
    endif()
    foreach(property IN LISTS properties)
        get_target_property(requirements "${target}" "${property}")
        if(NOT requirements OR requirements MATCHES "-NOTFOUND$")
            continue()
        endif()
        _crux_consumer_values(values "${requirements}")
        foreach(value IN LISTS values)
            if(IS_ABSOLUTE "${value}")
                cmake_path(NORMAL_PATH value)
            endif()
            string(TOLOWER "${value}" lower)
            string(REPLACE "\\" "/" lower "${lower}")
            if(lower MATCHES "(^|[/;:=<>_-])(zephyr|boards?|soc|vendor|nxp|cmsis|nrfx|mcux|fsl)([/;:=<>_-]|$)"
               OR lower MATCHES "(^|/)(lib|-l)(zephyr|nxp|cmsis|nrfx|mcux|fsl)([_.-]|$)"
               OR lower MATCHES "(^|/)modules/hal(/|$)"
               OR lower MATCHES "(^|/)components/[^/]+/(src|host|zephyr|tests?)(/|$)"
               OR lower MATCHES "(^|/)(test_support|test-support)(/|$)"
               OR lower MATCHES "__zephyr__")
                message(FATAL_ERROR "native consumer requirement: ${target} ${property}: ${value}")
            endif()
            if(property MATCHES "^(INTERFACE_)?LINK_LIBRARIES(_DIRECT)?$" AND TARGET "${value}")
                _crux_check_consumer_target("${value}" "${visited}")
            endif()
        endforeach()
    endforeach()
endfunction()

# Call after the composition has finished declaring targets. Implementation-only
# PRIVATE requirements are not traversed; LINK_ONLY requirements
# are traversed because those remain part of the consumer's final link contract.
function(crux_assert_portable_usage)
    foreach(target IN LISTS ARGN)
        if(NOT TARGET "${target}")
            message(FATAL_ERROR "unknown consumer target: ${target}")
        endif()
        _crux_check_consumer_target("${target}" "")
    endforeach()
endfunction()

# A designated portable consumer must also keep its own PRIVATE requirements
# portable. Only this root's direct properties are inspected; providers retain
# control of their implementation-only dependencies.
function(crux_assert_portable_consumer)
    foreach(target IN LISTS ARGN)
        if(NOT TARGET "${target}")
            message(FATAL_ERROR "unknown consumer target: ${target}")
        endif()
        _crux_check_consumer_target("${target}" "" DIRECT)
    endforeach()
endfunction()
