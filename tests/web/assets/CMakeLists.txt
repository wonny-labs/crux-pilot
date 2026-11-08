# The packaged web assets (Issue #189) on the real web core and the fake transport, once for each
# content policy. The folder under site/ is packaged by the real packager at build time, with the
# same budget check as a product's build. These exist only when Crux builds its own tests.
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxWebAssets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxWebRoutes.cmake)
set(WEB_ASSETS_PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../../../components/web/assets)

foreach(style IN ITEMS strict inline)
    set(core web_assets_core_${style})
    add_library(${core} STATIC ${CRUX_WEB_SOURCES} ../fake_transport.c api_route.c)
    target_include_directories(${core} PUBLIC .. ../../../components/web/src)
    target_compile_definitions(${core} PUBLIC CRUX_WEB_TESTING=1 CRUX_WEB_EXCHANGE_MAX=3
        PRIVATE _GNU_SOURCE)
    target_link_libraries(${core} PUBLIC crux_web_adapter crux_web_api crux_access_test_composition)
    target_compile_options(${core} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    # Public routes: no session lookup runs for an asset, so none is configured by default.
    crux_web_assets(${core} FOLDER ${CMAKE_CURRENT_LIST_DIR}/site STYLE ${style}
        RESPONSE_DEADLINE_MS 3000)
    crux_web_routes(${core} SOURCES api_route.c FAKE_MANIFEST REVIEWED reviewed-routes.txt)

    add_executable(web_assets_${style} assets.c)
    target_link_libraries(web_assets_${style} PRIVATE ${core})
    target_include_directories(web_assets_${style} PRIVATE ${WEB_ASSETS_PRIVATE})
    target_compile_definitions(web_assets_${style} PRIVATE _GNU_SOURCE
        CRUX_ASSETS_INLINE_STYLES=$<STREQUAL:${style},inline>
        ASSETS_EXPECT_INLINE=$<STREQUAL:${style},inline>)
    target_compile_options(web_assets_${style} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME Web.assets_${style} COMMAND web_assets_${style})
    set_tests_properties(Web.assets_${style} PROPERTIES TIMEOUT 60)
endforeach()

# The private header, which only the generated source and the handler include, must also compile as C++.
add_library(web_assets_cpp_header OBJECT cpp_header.cpp)
target_include_directories(web_assets_cpp_header PRIVATE ${WEB_ASSETS_PRIVATE})
target_compile_definitions(web_assets_cpp_header PRIVATE CRUX_ASSETS_INLINE_STYLES=0)
target_link_libraries(web_assets_cpp_header PRIVATE crux_web_adapter crux_web_api)
target_compile_options(web_assets_cpp_header PRIVATE -Wall -Wextra -Wpedantic -Werror)
