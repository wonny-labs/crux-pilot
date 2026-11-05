include_guard(GLOBAL)

# Packages a built web folder into a web image (Issue #189), with no Node.js: a host-side Python tool
# compresses and hashes each file, checks the budget and the bundle, and writes a C source of data, a
# private asset table and one CRUX_WEB_ROUTE per asset. Call it before crux_web_routes() for the same
# target, which then reads the generated source and generates one registered resource for each asset.
#
#   crux_web_assets(<target> FOLDER <dir> [STYLE strict|inline] [RESPONSE_DEADLINE_MS <ms>]
#                   [CLIENT_RATE <bytes/s>] [MAX_FILES <n>] [MAX_TOTAL_BYTES <n>] [DENY_STRINGS <file>])
#
# FOLDER     The built folder, with index.html at its top. A missing folder fails to configure.
# STYLE      The content policy for styles: strict, the default, or inline for a framework that injects
#            styles at run time. Scripts stay 'self' and frame-ancestors stays 'none' in both. There is no
#            way to give a policy string.
# RESPONSE_DEADLINE_MS, CLIENT_RATE
#            The largest asset is deadline x rate. The deadline defaults to
#            CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS in a Zephyr image. See docs/web-assets.md.
# MAX_FILES, MAX_TOTAL_BYTES
#            The budget, with the packager's declared defaults. The build fails when exceeded.
# DENY_STRINGS
#            A file of strings, one per line, that must not appear in any file of the bundle.
#
# Sets these target properties: CRUX_WEB_ASSETS_SOURCE, the generated source, and
# CRUX_WEB_ASSETS_HASH_LIST, the JSON list of asset hashes for the product's SBOM.
function(crux_web_assets target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG ""
        "FOLDER;STYLE;RESPONSE_DEADLINE_MS;CLIENT_RATE;MAX_FILES;MAX_TOTAL_BYTES;DENY_STRINGS" "")
    if(ARG_UNPARSED_ARGUMENTS OR NOT ARG_FOLDER)
        message(FATAL_ERROR "crux_web_assets(<target> FOLDER <dir> ...)")
    endif()
    get_target_property(routes_done ${target} CRUX_WEB_ROUTES_CONFIGURED)
    if(routes_done)
        message(FATAL_ERROR "crux_web_assets(${target}) must come before crux_web_routes(${target})")
    endif()
    if(NOT ARG_STYLE)
        set(ARG_STYLE strict)
    endif()
    if(NOT ARG_STYLE STREQUAL "strict" AND NOT ARG_STYLE STREQUAL "inline")
        message(FATAL_ERROR "crux_web_assets: STYLE is strict or inline, not '${ARG_STYLE}'. "
            "No other policy is offered.")
    endif()
    if(NOT ARG_RESPONSE_DEADLINE_MS)
        if(DEFINED CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS)
            set(ARG_RESPONSE_DEADLINE_MS "${CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS}")
        else()
            message(FATAL_ERROR "crux_web_assets: RESPONSE_DEADLINE_MS is needed outside Zephyr, because "
                "the largest asset is sized from the response deadline")
        endif()
    endif()
    cmake_path(ABSOLUTE_PATH ARG_FOLDER BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    if(NOT IS_DIRECTORY "${ARG_FOLDER}")
        message(FATAL_ERROR "crux_web_assets: the web folder ${ARG_FOLDER} does not exist. Build the "
            "frontend first: Crux needs no Node.js, but it needs the folder that a tool built.")
    endif()
    if(NOT EXISTS "${ARG_FOLDER}/index.html")
        message(FATAL_ERROR "crux_web_assets: ${ARG_FOLDER} has no index.html at its top, and / serves it")
    endif()

    set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/web/tools/web_assets.py")
    get_filename_component(runtime "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/web/assets" ABSOLUTE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/crux_web_assets/${target}")
    set(source "${directory}/crux_web_assets.c")
    set(hash_list "${directory}/crux-web-assets.json")
    set(arguments "${ARG_FOLDER}" --c-out "${source}" --hash-list "${hash_list}" --style "${ARG_STYLE}"
        --response-deadline-ms "${ARG_RESPONSE_DEADLINE_MS}")
    if(ARG_CLIENT_RATE)
        list(APPEND arguments --client-rate "${ARG_CLIENT_RATE}")
    endif()
    if(ARG_MAX_FILES)
        list(APPEND arguments --max-files "${ARG_MAX_FILES}")
    endif()
    if(ARG_MAX_TOTAL_BYTES)
        list(APPEND arguments --max-total-bytes "${ARG_MAX_TOTAL_BYTES}")
    endif()
    set(deny)
    if(ARG_DENY_STRINGS)
        cmake_path(ABSOLUTE_PATH ARG_DENY_STRINGS BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        list(APPEND arguments --deny-file "${ARG_DENY_STRINGS}")
        set(deny "${ARG_DENY_STRINGS}")
    endif()
    # A changed or new file in the folder runs the packager again, hidden files included.
    file(GLOB_RECURSE contents CONFIGURE_DEPENDS LIST_DIRECTORIES false "${ARG_FOLDER}/*")
    add_custom_command(OUTPUT "${source}" "${hash_list}"
        COMMAND "${Python3_EXECUTABLE}" "${tool}" ${arguments}
        DEPENDS "${tool}" ${contents} ${deny}
        COMMENT "Packaging the web folder of ${target}"
        VERBATIM)
    # Building this target alone packages and checks the folder, for the hash list and a budget check.
    add_custom_target(crux_web_assets_${target} DEPENDS "${source}" "${hash_list}")
    target_sources(${target} PRIVATE "${source}" "${runtime}/web_assets.c")
    # The private header is for these two sources. crux_web_adapter carries only the adapter's own.
    set_source_files_properties("${source}" "${runtime}/web_assets.c" PROPERTIES
        INCLUDE_DIRECTORIES "${runtime}")
    target_link_libraries(${target} PRIVATE crux_web_adapter)
    set_target_properties(${target} PROPERTIES CRUX_WEB_ASSETS_SOURCE "${source}"
        CRUX_WEB_ASSETS_HASH_LIST "${hash_list}")
endfunction()
