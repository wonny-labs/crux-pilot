# The status and configuration routes of Issue #187, on the real Web core, session module, status service,
# configuration service and Access core, with the fake Access ports, the fake Configuration store, fake
# status sources, fake session ports and the fake transport. All of these exist only when Crux builds its
# own tests. The configuration service is compiled here, as its own suites do, so that a save waits 50 ms
# for the service lock and not the 5 s of a product.
add_library(crux_web_management_test_core STATIC ${CRUX_WEB_SOURCES} ${CRUX_WEB_SESSION_SOURCES}
    ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_SOURCES} ${CRUX_WEB_MANAGEMENT_ROUTES}
    ../../../components/config_service/src/config_service.c
    ../fake_transport.c ../session/session_test.c ../session/test_routes.c management_test.c)
target_include_directories(crux_web_management_test_core PUBLIC . .. ../session
    ../../../components/web/src ${CRUX_WEB_SESSION_DIR} ${CRUX_WEB_MANAGEMENT_DIR})
target_compile_definitions(crux_web_management_test_core PUBLIC CRUX_WEB_TESTING=1
    CRUX_WEB_MANAGEMENT_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3 CRUX_WEB_SESSION_MAX=10
    CRUX_CONFIG_SERVICE_SAVE_WAIT_MS=50u PRIVATE _GNU_SOURCE)
target_link_libraries(crux_web_management_test_core PUBLIC crux_web_adapter crux_web_api
    crux_access_test_composition crux_config_service_api crux_config_service_fake_events
    crux_configuration_fake_provider crux_mgmt_status_core crux_mgmt_status_fake_sources
    crux::sync crux::thread Threads::Threads)
target_compile_options(crux_web_management_test_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
crux_web_routes(crux_web_management_test_core SOURCES ${CRUX_WEB_SESSION_ROUTES}
    ${CRUX_WEB_MANAGEMENT_ROUTES} ../session/test_routes.c FAKE_MANIFEST
    REVIEWED reviewed-routes.txt)

foreach(suite IN ITEMS status config save account fuzz openapi_fixture)
    add_executable(web_management_${suite} ${suite}.c)
    target_link_libraries(web_management_${suite} PRIVATE crux_web_management_test_core)
    target_compile_options(web_management_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(web_management_${suite} PRIVATE _GNU_SOURCE)
    if(NOT suite STREQUAL "openapi_fixture")
        add_test(NAME Web.management_${suite} COMMAND web_management_${suite})
        set_tests_properties(Web.management_${suite} PROPERTIES TIMEOUT 240)
    endif()
endforeach()

# What the routes answer, held to the schemas and statuses of the merged specification, with negative controls.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME Web.management_openapi
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check_openapi_config.py
        --fixture $<TARGET_FILE:web_management_openapi_fixture> --root ${PROJECT_SOURCE_DIR})
set_tests_properties(Web.management_openapi PROPERTIES TIMEOUT 240)

# The management header must also compile for a C++ composition.
add_library(web_management_cpp_header OBJECT cpp_header.cpp)
target_include_directories(web_management_cpp_header PRIVATE ${CRUX_WEB_MANAGEMENT_DIR})
target_link_libraries(web_management_cpp_header PRIVATE crux_web_adapter crux_web_api)
target_compile_options(web_management_cpp_header PRIVATE -Wall -Wextra -Wpedantic -Werror)
