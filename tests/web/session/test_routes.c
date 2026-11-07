/* Routes that only the session suites define: one that needs a permission, and a session-only one
 * on every method, for the forgery rules. */
#include <crux/web.h>

#include <string.h>

crux_access_session_t session_test_route_session;
unsigned session_test_route_calls;

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    static const char body[] = "{\"ok\":true}";

    (void)context;
    session_test_route_session = session;
    ++session_test_route_calls;
    return crux_web_respond(request, 200, NULL, body, strlen(body));
}

CRUX_WEB_ROUTE(test_admin, "/api/v1/test/admin", CRUX_WEB_GET | CRUX_WEB_POST, "access.manage",
               on_route, NULL);
CRUX_WEB_ROUTE(test_session, "/api/v1/test/session",
               CRUX_WEB_GET | CRUX_WEB_POST | CRUX_WEB_PUT | CRUX_WEB_DELETE | CRUX_WEB_PATCH,
               CRUX_WEB_SESSION, on_route, NULL);
