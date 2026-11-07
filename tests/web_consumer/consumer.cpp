// A project's route, through the public header alone, as C++17.
#include <crux/web.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond_error(request, 404, "not_found");
}

CRUX_WEB_ROUTE(pump_cpp, "/api/v1/app/cpp", CRUX_WEB_GET, CRUX_WEB_SESSION, on_route, nullptr);
