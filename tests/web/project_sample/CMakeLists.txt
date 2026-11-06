# The small example project of Issue #220, samples/web_project, on the real web core and the
# fake transport: its folder packaged by the real packager under the strict setting, and its route
# registered through the public route contract, with no native header. These exist only when Crux
# builds its own tests.
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxWebAssets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxWebRoutes.cmake)
set(project_sample ${CMAKE_CURRENT_LIST_DIR}/../../../samples/web_project)

add_library(web_project_sample_core STATIC ${CRUX_WEB_SOURCES} ../fake_transport.c ../web_test.c
    ${project_sample}/src/greeting_route.c)
target_include_directories(web_project_sample_core PUBLIC .. ../../../components/web/src)
target_compile_definitions(web_project_sample_core PUBLIC CRUX_WEB_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3
    PRIVATE _GNU_SOURCE)
target_link_libraries(web_project_sample_core PUBLIC crux_web_adapter crux_web_api
    crux_access_test_composition)
target_compile_options(web_project_sample_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
# What the project writes, as its README says: the folder and the route source.
crux_web_assets(web_project_sample_core FOLDER ${project_sample}/site STYLE strict
    RESPONSE_DEADLINE_MS 3000)
crux_web_routes(web_project_sample_core SOURCES ${project_sample}/src/greeting_route.c FAKE_MANIFEST
    REVIEWED ${CMAKE_CURRENT_LIST_DIR}/../../../samples/web_project/reviewed-routes.txt)

add_executable(web_project_sample project_sample.c)
target_link_libraries(web_project_sample PRIVATE web_project_sample_core)
target_compile_definitions(web_project_sample PRIVATE _GNU_SOURCE)
target_compile_options(web_project_sample PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME Web.project_sample COMMAND web_project_sample)
set_tests_properties(Web.project_sample PROPERTIES TIMEOUT 60)
