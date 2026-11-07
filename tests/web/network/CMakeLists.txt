# GET and PUT /api/v1/network of Issue #213, on the real Web core, session module, Access core,
# Configuration service and network settings service, with the fake Access ports, the fake Configuration
# store, the fake apply and event ports of tests/network_settings, fake session ports and the fake
# transport. All of these exist only when Crux builds its own tests. The Configuration service and the
# network settings service are compiled by tests/network_settings, with a save that waits 50 ms for the
# service lock and not the 5 s of a product.
add_library(crux_web_network_test_core STATIC ${CRUX_WEB_SOURCES} ${CRUX_WEB_SESSION_SOURCES}
    ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_SOURCES} ${CRUX_WEB_MANAGEMENT_NETWORK_SOURCES}
    ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES}
    ../fake_transport.c ../session/session_test.c ../session/test_routes.c network_web.c)
target_include_directories(crux_web_network_test_core PUBLIC . .. ../session
    ../../../components/web/src ${CRUX_WEB_SESSION_DIR} ${CRUX_WEB_MANAGEMENT_DIR})
target_compile_definitions(crux_web_network_test_core PUBLIC CRUX_WEB_TESTING=1
    CRUX_WEB_EXCHANGE_MAX=3 CRUX_WEB_SESSION_MAX=10 PRIVATE _GNU_SOURCE)
target_link_libraries(crux_web_network_test_core PUBLIC crux_web_adapter crux_web_api
    network_settings_test_fixture crux_mgmt_status_api crux::sync crux::thread Threads::Threads)
target_compile_options(crux_web_network_test_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
crux_web_routes(crux_web_network_test_core SOURCES ${CRUX_WEB_SESSION_ROUTES}
    ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES} ../session/test_routes.c FAKE_MANIFEST
    REVIEWED reviewed-routes.txt)

foreach(suite IN ITEMS routes fuzz short_reads openapi_fixture)
    add_executable(web_network_${suite} ${suite}.c $<TARGET_OBJECTS:network_settings_test_fixture>)
    target_link_libraries(web_network_${suite} PRIVATE crux_web_network_test_core
        crux_configuration_fake_provider)
    target_compile_options(web_network_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(web_network_${suite} PRIVATE _GNU_SOURCE)
    if(NOT suite STREQUAL "openapi_fixture")
        add_test(NAME Web.network_${suite} COMMAND web_network_${suite})
        set_tests_properties(Web.network_${suite} PROPERTIES TIMEOUT 240)
    endif()
endforeach()

target_link_options(web_network_short_reads PRIVATE -Wl,--wrap=crux_web_read_body)

# Over the no-provider port: the 501.
foreach(suite IN ITEMS no_provider openapi_fixture)
    set(target web_network_${suite}_no_provider)
    if(suite STREQUAL "no_provider")
        set(target web_network_no_provider)
    endif()
    add_executable(${target} ${suite}.c fault_stubs.c $<TARGET_OBJECTS:network_settings_test_fixture>)
    target_link_libraries(${target} PRIVATE crux_web_network_test_core crux_configuration_no_provider)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(${target} PRIVATE _GNU_SOURCE FIXTURE_NO_PROVIDER=1)
endforeach()
add_test(NAME Web.network_no_provider COMMAND web_network_no_provider)
set_tests_properties(Web.network_no_provider PROPERTIES TIMEOUT 120)

# What the routes answer, held to the schemas and statuses of the merged specification, with negative controls.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME Web.network_openapi
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/check_openapi_network.py
        --fixture $<TARGET_FILE:web_network_openapi_fixture>
        --fixture $<TARGET_FILE:web_network_openapi_fixture_no_provider>
        --root ${PROJECT_SOURCE_DIR})
set_tests_properties(Web.network_openapi PROPERTIES TIMEOUT 240)

# The reader's header must also compile for a C++ composition.
add_library(web_network_cpp_header OBJECT cpp_header.cpp)
target_include_directories(web_network_cpp_header PRIVATE ${CRUX_WEB_MANAGEMENT_DIR})
target_link_libraries(web_network_cpp_header PRIVATE crux_network_settings_api)
target_compile_options(web_network_cpp_header PRIVATE -Wall -Wextra -Wpedantic -Werror)
