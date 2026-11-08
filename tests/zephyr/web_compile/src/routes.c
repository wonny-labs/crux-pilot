/* One fixed route and one wildcard route, so that both kinds of resource link. */
#include <crux/web.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond_error(request, 404, "not_found");
}

CRUX_WEB_ROUTE(fixed, "/api/v1/status", CRUX_WEB_GET, CRUX_WEB_SESSION, on_route, NULL);
CRUX_WEB_ROUTE(wildcard, "/api/v1/session/attempt/{id}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_route,
               NULL);
