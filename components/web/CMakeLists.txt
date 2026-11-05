set(CRUX_WEB_SOURCES
    src/web_core.c
    src/web_response.c
    src/web_route.c
)
list(TRANSFORM CRUX_WEB_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_WEB_SOURCES "${CRUX_WEB_SOURCES}" CACHE INTERNAL "Portable Web sources")

# The largest request body a route can accept, which sizes the core's buffer per exchange.
set(CRUX_WEB_BODY_BUFFER 1024 CACHE STRING "Bytes of request body the core holds per exchange")

add_library(crux_web_api INTERFACE)
target_include_directories(crux_web_api INTERFACE include)
target_compile_definitions(crux_web_api INTERFACE CRUX_WEB_BODY_BUFFER=${CRUX_WEB_BODY_BUFFER}u)
target_link_libraries(crux_web_api INTERFACE crux_access_api)

# Internal to transport adapters and the session and asset compositions. Link it PRIVATE, so that its
# include directory never reaches a project.
add_library(crux_web_adapter INTERFACE)
target_include_directories(crux_web_adapter INTERFACE adapter)
target_link_libraries(crux_web_adapter INTERFACE crux_web_api)

# The core has no transport and no route table. A web image links a platform adapter and the table
# that cmake/CruxWebRoutes.cmake generates.
add_library(crux_web_core STATIC ${CRUX_WEB_SOURCES})
target_include_directories(crux_web_core PRIVATE src)
target_link_libraries(crux_web_core PUBLIC crux_web_api PRIVATE crux_web_adapter crux::access)
target_compile_options(crux_web_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_web INTERFACE)
add_library(crux::web ALIAS crux_web)
target_link_libraries(crux_web INTERFACE crux_web_api crux_web_core)

# Login, sessions and request forgery protection (Issue #186): sources that a composition compiles
# with the core, with crux::sync, crux::thread and the ports of session/web_session_port.h.
include(${CMAKE_CURRENT_LIST_DIR}/session/sources.cmake)
