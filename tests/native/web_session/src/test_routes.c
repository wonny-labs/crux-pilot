/* Routes of the test image only: one that needs a permission, a session-only one for the forgery
 * rules, and the counters that the driver reads for its measurement. */
#include "crux_web_session.h"

#include <stdio.h>
#include <string.h>

static crux_web_result_t on_ok(crux_web_request_t *request, crux_access_session_t session,
                               void *context)
{
    static const char body[] = "{\"ok\":true}";

    (void)session;
    (void)context;
    return crux_web_respond(request, 200, NULL, body, strlen(body));
}

static crux_web_result_t on_stats(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    crux_web_session_stats_t sessions;
    crux_web_stats_t web;
    char body[320];
    int length;

    (void)session;
    (void)context;
    crux_web_session_stats_get(&sessions);
    crux_web_stats_get(&web);
    length = snprintf(body, sizeof(body),
                      "{\"attempts\":%u,\"busy\":%u,\"verified\":%u,\"expired\":%u,"
                      "\"sessions\":%u,\"worker_max_ms\":%u,\"handler_max_ms\":%u,"
                      "\"handler_overruns\":%u}",
                      (unsigned)sessions.attempts, (unsigned)sessions.busy,
                      (unsigned)sessions.verified, (unsigned)sessions.expired,
                      (unsigned)sessions.sessions, (unsigned)sessions.worker_max_ms,
                      (unsigned)web.handler_max_ms, (unsigned)web.handler_overruns);
    return crux_web_respond(request, 200, NULL, body, (size_t)length);
}

CRUX_WEB_ROUTE(test_admin, "/api/v1/test/admin", CRUX_WEB_GET | CRUX_WEB_POST, "access.manage",
               on_ok, NULL);
CRUX_WEB_ROUTE(test_session, "/api/v1/test/session", CRUX_WEB_GET | CRUX_WEB_POST, CRUX_WEB_SESSION,
               on_ok, NULL);
CRUX_WEB_ROUTE(probe_stats, "/probe/session-stats", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_stats, NULL);
