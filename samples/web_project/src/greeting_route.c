/*
 * A project's own API route, beside the management routes, through the public route contract alone:
 * <crux/web.h> and no Zephyr, board or other native header. The route needs a signed-in session,
 * which the composition's session lookup checks before this handler runs. See README.md.
 */
#include <crux/web.h>

#include <string.h>

static const char greeting[] = "{\"greeting\":\"Hello from the project route\"}";

static crux_web_result_t on_greeting(crux_web_request_t *request, crux_access_session_t session,
                                     void *context)
{
    (void)session;
    (void)context;
    return crux_web_respond(request, 200, NULL, greeting, strlen(greeting));
}

CRUX_WEB_ROUTE(project_greeting, "/api/v1/project/greeting", CRUX_WEB_GET, CRUX_WEB_SESSION,
               on_greeting, NULL);
