# The real web core and the real Access core, on fake Access ports, a fake session lookup and a fake
# transport. All of these exist only when Crux builds its own tests.
find_package(Threads REQUIRED)
include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/CruxWebRoutes.cmake)

add_library(crux_web_test_core STATIC ${CRUX_WEB_SOURCES} web_test.c fake_transport.c routes_fixture.c)
target_include_directories(crux_web_test_core PUBLIC . ../../components/web/src)
target_compile_definitions(crux_web_test_core PUBLIC CRUX_WEB_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3
    PRIVATE _GNU_SOURCE)
target_link_libraries(crux_web_test_core PUBLIC crux_web_adapter crux_web_api crux_access_test_composition)
target_compile_options(crux_web_test_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
crux_web_routes(crux_web_test_core SOURCES routes_fixture.c FAKE_MANIFEST)

foreach(suite IN ITEMS routing enforcement requests)
    add_executable(web_${suite} ${suite}.c)
    target_link_libraries(web_${suite} PRIVATE crux_web_test_core)
    target_compile_options(web_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME Web.${suite} COMMAND web_${suite})
    set_tests_properties(Web.${suite} PROPERTIES TIMEOUT 60)
endforeach()

# Every public and adapter declaration must also compile for a C++ consumer.
add_library(web_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(web_cpp_headers PRIVATE crux_web_adapter crux_web_api)
target_compile_options(web_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The session module on the real web and Access cores (Issue #186).
if(TARGET crux::thread)
    add_subdirectory(session)
endif()

# The packaged web assets (Issue #189) on the same core and fake transport.
add_subdirectory(assets)

# The status and configuration routes on the real services (Issue #187).
if(TARGET crux::thread AND TARGET crux_config_service_fake_events AND TARGET crux_mgmt_status_core
   AND TARGET crux_mgmt_status_fake_sources)
    add_subdirectory(management)
endif()

# GET and PUT /api/v1/network over the network settings service (Issue #213).
if(TARGET crux::thread AND TARGET network_settings_test_fixture AND TARGET crux_mgmt_status_api)
    add_subdirectory(network)
endif()

# GET and POST /api/v1/system/reboot over the reboot service (Issue #214).
if(TARGET crux::thread AND TARGET reboot_test_fixture)
    add_subdirectory(reboot)
endif()
