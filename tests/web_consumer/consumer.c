/* A project's routes, through the public header alone, as C17. */
#include <crux/web.h>

static crux_web_result_t on_pump_status(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond(request, 200, NULL, "{\"speed\":42}", 12u);
}

static crux_web_result_t on_pump_item(crux_web_request_t *request, crux_access_session_t session,
                                      void *context)
{
    char item[16];

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, item, sizeof(item)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    return crux_web_respond(request, 200, "text/plain", item, 0u);
}

CRUX_WEB_ROUTE(pump_status, "/api/v1/app/pump", CRUX_WEB_GET, "app.pump.read", on_pump_status,
               NULL);
CRUX_WEB_ROUTE(pump_item, "/api/v1/app/pump/{id}", CRUX_WEB_GET, CRUX_WEB_SESSION, on_pump_item,
               NULL);
