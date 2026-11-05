// The public and adapter headers, and a route definition, compile for a C++ consumer.
#include "crux_web_adapter.h"

#include <crux/web.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond_error(request, 404, "not_found");
}

CRUX_WEB_ROUTE(cpp_route, "/cpp/route", CRUX_WEB_GET, CRUX_WEB_SESSION, on_route, nullptr);
