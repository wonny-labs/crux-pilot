/* Must fail to compile: a body limit over the buffer */
#include <crux/web.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond_error(request, 404, "not_found");
}

CRUX_WEB_ROUTE_BODY(route, "/x", CRUX_WEB_POST, CRUX_WEB_SESSION, on_route, NULL,
                    CRUX_WEB_BODY_BUFFER + 1u);
