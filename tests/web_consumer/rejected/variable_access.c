/* Must fail to compile: a variable as the access class */
#include <crux/web.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond_error(request, 404, "not_found");
}

static const char *const permission = "a.b";
CRUX_WEB_ROUTE(route, "/x", CRUX_WEB_GET, permission, on_route, NULL);
