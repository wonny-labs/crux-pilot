include_guard(GLOBAL)

# Generates the route table of a web image from the sources that hold its CRUX_WEB_ROUTE uses, and
# adds it to a target that links crux::web, along with those sources' routes. A route in a source not
# named here is not in the table, so it cannot be reached.
#
#   crux_web_routes(<target> SOURCES <files> [REVIEWED <file>] [ZEPHYR_RESOURCES] [FAKE_MANIFEST])
#
# REVIEWED fails the build on a public or session-only route that is not on that list, or an entry with
# no route. This is the check that sees generated sources, such as the asset routes of #189, and any
# project layout, so an image with ZEPHYR_RESOURCES must give it. See scripts/check-web-routes.py for the
# list's form.
# ZEPHYR_RESOURCES also generates the registered resources, and needs the Zephyr port's header and
# Zephyr's headers on the target, as `app` has. FAKE_MANIFEST is for the Host's fake transport only.
function(crux_web_routes target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "ZEPHYR_RESOURCES;FAKE_MANIFEST" "REVIEWED" "SOURCES")
    if(NOT ARG_SOURCES OR ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "crux_web_routes(<target> SOURCES <files> ...)")
    endif()
    if(ARG_ZEPHYR_RESOURCES AND NOT ARG_REVIEWED)
        message(FATAL_ERROR "crux_web_routes: an image with ZEPHYR_RESOURCES needs REVIEWED <file>, "
            "the list of its public and session-only routes")
    endif()
    set(tool "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../components/web/tools/web_routes.py")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    # The packaged web folder of crux_web_assets() (Issue #189): its generated routes are scanned as a
    # bundle, which the reviewed list holds as the one entry `public GET @assets`.
    get_target_property(assets_source ${target} CRUX_WEB_ASSETS_SOURCE)
    set(sources)
    foreach(source IN LISTS ARG_SOURCES)
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        list(APPEND sources "${source}")
    endforeach()
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/crux_web_generated/${target}")
    set(outputs "${directory}/crux_web_routes.c")
    set(arguments --table "${directory}/crux_web_routes.c")
    if(ARG_ZEPHYR_RESOURCES)
        list(APPEND outputs "${directory}/crux_web_resources.c")
        list(APPEND arguments --zephyr "${directory}/crux_web_resources.c")
    endif()
    if(ARG_FAKE_MANIFEST)
        list(APPEND outputs "${directory}/fake_resources.c")
        list(APPEND arguments --manifest "${directory}/fake_resources.c")
    endif()
    set(bundle)
    if(assets_source)
        list(APPEND arguments --bundle "${assets_source}")
        set(bundle "${assets_source}")
    endif()
    set(reviewed)
    if(ARG_REVIEWED)
        cmake_path(ABSOLUTE_PATH ARG_REVIEWED BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        list(APPEND arguments --reviewed "${ARG_REVIEWED}")
        set(reviewed "${ARG_REVIEWED}")
    endif()
    add_custom_command(OUTPUT ${outputs}
        COMMAND "${Python3_EXECUTABLE}" "${tool}" ${arguments} ${sources}
        DEPENDS "${tool}" ${sources} ${bundle} ${reviewed}
        COMMENT "Generating the web route table of ${target}"
        VERBATIM)
    target_sources(${target} PRIVATE ${outputs})
    set_target_properties(${target} PROPERTIES CRUX_WEB_ROUTES_CONFIGURED TRUE)
    if(ARG_ZEPHYR_RESOURCES)
        target_link_libraries(${target} PRIVATE crux_web_zephyr_resources)
    endif()
endfunction()
