# The session module on the real Web and Access cores, the fake Access ports, fake session ports and
# the fake transport of tests/web. All of these exist only when Crux builds its own tests.
add_library(crux_web_session_test_core STATIC ${CRUX_WEB_SOURCES} ${CRUX_WEB_SESSION_SOURCES}
    ${CRUX_WEB_SESSION_ROUTES} ../fake_transport.c session_test.c test_routes.c)
target_include_directories(crux_web_session_test_core PUBLIC . .. ../../../components/web/src
    ${CRUX_WEB_SESSION_DIR})
# A table of three sessions shows what a full one does with the three test accounts.
target_compile_definitions(crux_web_session_test_core PUBLIC CRUX_WEB_TESTING=1
    CRUX_WEB_SESSION_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3 CRUX_WEB_SESSION_MAX=3 PRIVATE _GNU_SOURCE)
target_link_libraries(crux_web_session_test_core PUBLIC crux_web_adapter crux_web_api
    crux_access_test_composition crux::sync crux::thread)
target_compile_options(crux_web_session_test_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
crux_web_routes(crux_web_session_test_core SOURCES ${CRUX_WEB_SESSION_ROUTES} test_routes.c
    FAKE_MANIFEST REVIEWED reviewed-routes.txt)

foreach(suite IN ITEMS login sessions json concurrency)
    add_executable(web_session_${suite} ${suite}.c)
    target_link_libraries(web_session_${suite} PRIVATE crux_web_session_test_core)
    target_compile_options(web_session_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(web_session_${suite} PRIVATE _GNU_SOURCE)
    add_test(NAME Web.session_${suite} COMMAND web_session_${suite})
    set_tests_properties(Web.session_${suite} PROPERTIES TIMEOUT 120)
endforeach()

# The session header must also compile for a C++ composition.
add_library(web_session_cpp_header OBJECT cpp_header.cpp)
target_include_directories(web_session_cpp_header PRIVATE ${CRUX_WEB_SESSION_DIR}
    ../../../components/access/adapter)
target_link_libraries(web_session_cpp_header PRIVATE crux_web_adapter crux_web_api)
target_compile_options(web_session_cpp_header PRIVATE -Wall -Wextra -Wpedantic -Werror)
