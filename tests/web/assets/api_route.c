/* A project's own API route beside the assets: the assets must not shadow it, or it them. */
#include <crux/web.h>

static crux_web_result_t on_ping(crux_web_request_t *request, crux_access_session_t session,
                                 void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond(request, 200, NULL, "{\"ping\":true}", 13u);
}

CRUX_WEB_ROUTE(api_ping, "/api/v1/ping", CRUX_WEB_GET, "ping.read", on_ping, NULL);
