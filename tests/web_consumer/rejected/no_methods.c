/* Must fail to compile: no methods */
#include <crux/web.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond_error(request, 404, "not_found");
}

CRUX_WEB_ROUTE(route, "/x", 0u, CRUX_WEB_SESSION, on_route, NULL);
