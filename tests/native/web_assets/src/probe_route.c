/* One probe, so that a test can make the server block part-way through a large asset: it shrinks
 * the send buffer of the connection that asks, as on a small target. Not for a product. */
#include "web_probe.h"

#include <crux/web.h>
#include <stdlib.h>

static crux_web_result_t on_send_buffer(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    char value[16];

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, value, sizeof(value)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    web_probe_set_next_send_buffer(atoi(value));
    return crux_web_respond(request, 200, NULL, "{}", 2u);
}

CRUX_WEB_ROUTE(send_buffer, "/probe/send-buffer/{bytes}", CRUX_WEB_GET, CRUX_WEB_PUBLIC,
               on_send_buffer, NULL);
