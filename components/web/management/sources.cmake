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

# Network settings routes (Issue #213): GET and PUT /api/v1/network, for a composition that offers
# the network settings service. Compile the sources with crux::network_settings beside the sources
# above, and name the routes source to crux_web_routes(... SOURCES ...) beside the one above. Both
# routes have a permission as their access class, so components/web/reviewed-routes.txt does not
# list them.
set(CRUX_WEB_MANAGEMENT_NETWORK_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/web_management_network_json.c"
    CACHE INTERNAL "Web management network sources")
set(CRUX_WEB_MANAGEMENT_NETWORK_ROUTES "${CMAKE_CURRENT_LIST_DIR}/web_management_network.c"
    CACHE INTERNAL "Web management network routes")

# Reboot routes (Issue #214): GET and POST /api/v1/system/reboot, for a composition that includes the
# reboot service. Name this one source to crux_web_routes(... SOURCES ...) and link crux::reboot. A
# composition that leaves it out has no reboot route. It needs none of the sources above, and both
# routes have a permission as their access class, so components/web/reviewed-routes.txt does not list
# them.
set(CRUX_WEB_MANAGEMENT_REBOOT_ROUTES "${CMAKE_CURRENT_LIST_DIR}/web_management_reboot.c"
    CACHE INTERNAL "Web management reboot routes")
