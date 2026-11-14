# GET and POST /api/v1/system/reboot of Issue #214, on the real Web core, session module, Access core and
# reboot service, with the fakes of tests/reboot, fake session ports and the fake transport. All of these
# exist only when Crux builds its own tests.
#
# Two cores are built from the same sources, and they differ in one thing: whether the route table names
# the reboot routes. The first does, for the route suites. The second does not, and shows that a
# composition without the service has no reboot route. The service's core is the Zephyr-direct one,
# over the kernel stub of tests/reboot/host_kernel, as in tests/reboot.
find_package(Threads REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

function(crux_web_reboot_core target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "ROUTES")
    add_library(${target} STATIC ${CRUX_WEB_SOURCES} ${CRUX_WEB_SESSION_SOURCES}
        ${CRUX_WEB_SESSION_ROUTES} ${ARG_ROUTES}
        ../fake_transport.c ../session/session_test.c ../session/test_routes.c reboot_web.c)
    target_include_directories(${target} PUBLIC . .. ../session ../../../components/web/src
        ${CRUX_WEB_SESSION_DIR} ${CRUX_WEB_MANAGEMENT_DIR})
    target_compile_definitions(${target} PUBLIC CRUX_WEB_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3
        CRUX_WEB_SESSION_MAX=10 PRIVATE _GNU_SOURCE)
    target_link_libraries(${target} PUBLIC crux_web_adapter crux_web_api reboot_test_fixture
        crux::sync crux::thread Threads::Threads)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    crux_web_routes(${target} SOURCES ${CRUX_WEB_SESSION_ROUTES} ${ARG_ROUTES} ../session/test_routes.c
        FAKE_MANIFEST REVIEWED reviewed-routes.txt)
endfunction()

crux_web_reboot_core(crux_web_reboot_test_core ROUTES ${CRUX_WEB_MANAGEMENT_REBOOT_ROUTES})
crux_web_reboot_core(crux_web_no_reboot_test_core)

# The handlers' calls to crux_web_respond go through a wrapper that notes the status in the record of
# steps, which is how the suites see the answer between the event and the delay. A wrapped
# crux_mutex_lock fails the service's own wait on request, for the answer `busy`.
foreach(suite IN ITEMS routes openapi_fixture)
    add_executable(web_reboot_${suite} ${suite}.c $<TARGET_OBJECTS:reboot_test_fixture>
        $<TARGET_OBJECTS:reboot_fault_bridge>)
    target_link_libraries(web_reboot_${suite} PRIVATE crux_web_reboot_test_core)
    target_compile_options(web_reboot_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(web_reboot_${suite} PRIVATE _GNU_SOURCE)
    target_link_options(web_reboot_${suite} PRIVATE -Wl,--wrap=crux_web_respond
        -Wl,--wrap=crux_mutex_lock -Wl,--wrap=z_impl_k_mutex_lock)
endforeach()
add_test(NAME Web.reboot_routes COMMAND web_reboot_routes)
set_tests_properties(Web.reboot_routes PROPERTIES TIMEOUT 240)

add_executable(web_reboot_no_service no_service.c $<TARGET_OBJECTS:reboot_test_fixture>
    $<TARGET_OBJECTS:reboot_fault_bridge>)
target_link_libraries(web_reboot_no_service PRIVATE crux_web_no_reboot_test_core)
target_compile_options(web_reboot_no_service PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_definitions(web_reboot_no_service PRIVATE _GNU_SOURCE)
target_link_options(web_reboot_no_service PRIVATE -Wl,--wrap=crux_web_respond
    -Wl,--wrap=crux_mutex_lock -Wl,--wrap=z_impl_k_mutex_lock)
add_test(NAME Web.reboot_no_service COMMAND web_reboot_no_service)
set_tests_properties(Web.reboot_no_service PROPERTIES TIMEOUT 60)

# What the routes answer, held to the schemas and statuses of the merged specification, with negative
# controls.
add_test(NAME Web.reboot_openapi
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check_openapi_reboot.py
        --fixture $<TARGET_FILE:web_reboot_openapi_fixture> --root ${PROJECT_SOURCE_DIR})
set_tests_properties(Web.reboot_openapi PROPERTIES TIMEOUT 120)
