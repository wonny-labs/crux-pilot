# Sources of the session module (Issue #186), for a composition to compile with the web core. The
# routes source also goes to crux_web_routes(... SOURCES ...), and the composition supplies the ports
# of web_session_port.h, such as components/web/ports/psa/web_session_psa.c.
set(CRUX_WEB_SESSION_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "Web session directory")
set(CRUX_WEB_SESSION_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/web_session.c"
    "${CMAKE_CURRENT_LIST_DIR}/web_session_attempt.c"
    "${CMAKE_CURRENT_LIST_DIR}/web_session_json.c"
    CACHE INTERNAL "Web session sources")
set(CRUX_WEB_SESSION_ROUTES "${CMAKE_CURRENT_LIST_DIR}/web_session_routes.c" CACHE INTERNAL
    "Web session routes")
set(CRUX_WEB_SESSION_PSA_PORT "${CMAKE_CURRENT_LIST_DIR}/../ports/psa/web_session_psa.c" CACHE INTERNAL
    "Web session ports on PSA Crypto")
