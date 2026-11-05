# Sources of the status and configuration routes (Issue #187), for a composition to compile with the
# web core, crux::config_service, crux::mgmt_status, crux::sync and crux::thread. The routes source
# also goes to crux_web_routes(... SOURCES ...), and its session-only routes are listed in
# components/web/reviewed-routes.txt.
set(CRUX_WEB_MANAGEMENT_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "Web management directory")
set(CRUX_WEB_MANAGEMENT_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/web_management_json.c"
    "${CMAKE_CURRENT_LIST_DIR}/web_management_save.c"
    CACHE INTERNAL "Web management sources")
set(CRUX_WEB_MANAGEMENT_ROUTES "${CMAKE_CURRENT_LIST_DIR}/web_management_routes.c" CACHE INTERNAL
    "Web management routes")
