/* The API's shape for the driver, plus probes that measure the server thread. Not for a product. */
#include "web_probe.h"

#include <crux/web.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#define LARGE_BODY_SIZE (8u * 1024u * 1024u)
static uint8_t large_body[LARGE_BODY_SIZE];

void web_probe_fill_large_body(void)
{
    for (size_t index = 0; index < sizeof(large_body); ++index) {
        large_body[index] = (uint8_t)('a' + index % 26u);
    }
}

static crux_web_result_t respond_json(crux_web_request_t *request, const char *route)
{
    char body[96];
    int length = snprintf(body, sizeof(body), "{\"route\":\"%s\",\"body\":%zu}", route,
                          crux_web_body_length(request));

    return crux_web_respond(request, 200, NULL, body, (size_t)length);
}

static crux_web_result_t on_named(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    (void)session;
    return respond_json(request, (const char *)context);
}

static crux_web_result_t on_attempt(crux_web_request_t *request, crux_access_session_t session,
                                    void *context)
{
    char id[64];
    char query[64];
    char body[192];
    int length;

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, id, sizeof(id)) != CRUX_WEB_OK ||
        crux_web_query(request, query, sizeof(query)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    length = snprintf(body, sizeof(body), "{\"route\":\"attempt\",\"id\":\"%s\",\"query\":\"%s\"}",
                      id, query);
    return crux_web_respond(request, 200, NULL, body, (size_t)length);
}

/* The lengths of what the capture held, so the driver can size it without any value coming back. */
static crux_web_result_t on_echo(crux_web_request_t *request, crux_access_session_t session,
                                 void *context)
{
    const crux_web_request_head_t *head = crux_web_request_head(request);
    char body[256];
    int length;

#define LENGTH_OF(field) (head->field != NULL ? (int)strlen(head->field) : -1)
    (void)session;
    (void)context;
    length = snprintf(body, sizeof(body),
                      "{\"cookie\":%d,\"origin\":%d,\"host\":%d,\"xsrf\":%d,\"request\":%d,"
                      "\"type\":%d,\"length\":%d,\"body\":%zu}",
                      LENGTH_OF(cookie), LENGTH_OF(origin), LENGTH_OF(host), LENGTH_OF(xsrf_token),
                      LENGTH_OF(crux_request), LENGTH_OF(content_type), LENGTH_OF(content_length),
                      crux_web_body_length(request));
#undef LENGTH_OF
    return crux_web_respond(request, 200, NULL, body, (size_t)length);
}

/* Sets cookies of the size that #186 will: 256 bits in base64url, for the real-browser run. */
static crux_web_result_t on_browser_cookies(crux_web_request_t *request,
                                            crux_access_session_t session, void *context)
{
    (void)session;
    (void)context;
    if (crux_web_add_header(request, "Set-Cookie",
                            "crux_session=admin000000000000000000000000000000000000000; Secure; "
                            "HttpOnly; SameSite=Strict; Path=/") != CRUX_WEB_OK ||
        crux_web_add_header(request, "Set-Cookie",
                            "XSRF-TOKEN=0000000000000000000000000000000000000000000; Secure; "
                            "SameSite=Strict; Path=/") != CRUX_WEB_OK) {
        return CRUX_WEB_ERR_FAILED;
    }
    return crux_web_respond(request, 200, NULL, "{}", 2u);
}

static crux_web_result_t on_slow(crux_web_request_t *request, crux_access_session_t session,
                                 void *context)
{
    char text[12];

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, text, sizeof(text)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    k_msleep(atoi(text));
    return respond_json(request, "slow");
}

/* Bodies of a chosen size, sent in pieces by the adapter. The security headers are the caller's. */
static crux_web_result_t on_static(crux_web_request_t *request, crux_access_session_t session,
                                   void *context)
{
    static const crux_web_header_t headers[] = {
        {"Content-Type", "application/octet-stream"},
        {"X-Content-Type-Options", "nosniff"},
    };
    char text[12];

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, text, sizeof(text)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    return crux_web_respond_static(request, 200, headers, 2u, large_body,
                                   MIN((size_t)atoi(text), sizeof(large_body)));
}

/* A page for a real browser, whose script makes the requests a client makes, as the page itself. */
static crux_web_result_t on_page(crux_web_request_t *request, crux_access_session_t session,
                                 void *context)
{
    static const char page[] = "<!doctype html><title>probe</title><pre id=out></pre>"
                               "<script src=/probe/page.js></script>";

    (void)session;
    (void)context;
    return crux_web_respond(request, 200, "text/html", page, sizeof(page) - 1u);
}

static crux_web_result_t on_page_script(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    static const char script[] =
        "(async () => {\n"
        "  const out = document.getElementById('out');\n"
        "  try {\n"
        "    const token = document.cookie.split('; ').find(c => c.startsWith('XSRF-TOKEN='));\n"
        "    const post = await fetch('/probe/echo', {method: 'POST', headers: {\n"
        "      'Content-Type': 'application/json', 'X-XSRF-TOKEN': token.split('=')[1],\n"
        "      'X-Crux-Request': '1'}, body: '{}'});\n"
        "    const postBody = await post.json();\n"
        "    const get = await fetch('/probe/echo', {headers: {'X-Crux-Request': '1'}});\n"
        "    const getBody = await get.json();\n"
        "    const parallel = await Promise.all(Array.from({length: 8}, (_, i) =>\n"
        "      fetch('/probe/stats?' + i).then(r => r.status)));\n"
        "    out.textContent = JSON.stringify({userAgent: navigator.userAgent,\n"
        "      postStatus: post.status, post: postBody, get: getBody, parallel});\n"
        "  } catch (e) {\n"
        "    out.textContent = JSON.stringify({error: String(e)});\n"
        "  }\n"
        "})();\n";

    (void)session;
    (void)context;
    return crux_web_respond(request, 200, "text/javascript", script, sizeof(script) - 1u);
}

/* A response in pieces, whose chosen send returns -EAGAIN as a stalled reader's would. */
static crux_web_result_t on_fail(crux_web_request_t *request, crux_access_session_t session,
                                 void *context)
{
    static uint8_t body[4096];
    char text[12];

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, text, sizeof(text)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    memset(body, 'A', sizeof(body));
    web_probe_fail_send_after((unsigned)atoi(text));
    return crux_web_respond(request, 200, "text/plain", body, sizeof(body));
}

/* Another client's response, through the one shared buffer. */
static crux_web_result_t on_other(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    static uint8_t body[4096];

    (void)session;
    (void)context;
    memset(body, 'B', sizeof(body));
    return crux_web_respond(request, 200, "text/plain", body, sizeof(body));
}

static crux_web_result_t on_send_buffer(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    char text[12];

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, text, sizeof(text)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    web_probe_set_next_send_buffer(atoi(text));
    return crux_web_respond(request, 200, NULL, "{}", 2u);
}

static crux_web_result_t on_stats(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    crux_web_stats_t stats;
    char body[256];
    int length;

    (void)session;
    (void)context;
    crux_web_stats_get(&stats);
    length = snprintf(body, sizeof(body),
                      "{\"requests\":%u,\"rejected\":%u,\"handlers\":%u,\"deadlines\":%u,"
                      "\"response_deadlines\":%u,"
                      "\"aborted\":%u,\"overruns\":%u,\"handler_max_ms\":%u,\"send_eagain\":%u}",
                      stats.requests, stats.rejected, stats.handlers, stats.deadlines,
                      stats.response_deadlines, stats.aborted, stats.handler_overruns,
                      stats.handler_max_ms, web_probe_eagain_count());
    return crux_web_respond(request, 200, NULL, body, (size_t)length);
}

CRUX_WEB_ROUTE(login, "/api/v1/session", CRUX_WEB_POST, CRUX_WEB_PUBLIC, on_named, "login");
CRUX_WEB_ROUTE(logout, "/api/v1/session", CRUX_WEB_DELETE, CRUX_WEB_SESSION, on_named, "logout");
CRUX_WEB_ROUTE(attempt, "/api/v1/session/attempt/{id}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_attempt,
               NULL);
CRUX_WEB_ROUTE(me, "/api/v1/session/me", CRUX_WEB_GET, CRUX_WEB_SESSION, on_named, "me");
CRUX_WEB_ROUTE(status, "/api/v1/status", CRUX_WEB_GET, CRUX_WEB_SESSION, on_named, "status");
CRUX_WEB_ROUTE(network_read, "/api/v1/network", CRUX_WEB_GET, "network.read", on_named,
               "network_read");
CRUX_WEB_ROUTE(network_write, "/api/v1/network", CRUX_WEB_PUT, "network.write", on_named,
               "network_write");
CRUX_WEB_ROUTE(file_list, "/api/v1/files/{area}", CRUX_WEB_GET, CRUX_WEB_SESSION, on_named,
               "file_list");
CRUX_WEB_ROUTE(file_item, "/api/v1/files/{area}/{name}", CRUX_WEB_GET, CRUX_WEB_SESSION, on_named,
               "file_item");
CRUX_WEB_ROUTE(echo_get, "/probe/echo", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_echo, NULL);
CRUX_WEB_ROUTE(echo_post, "/probe/echo", CRUX_WEB_POST, CRUX_WEB_PUBLIC, on_echo, NULL);
CRUX_WEB_ROUTE(browser_cookies, "/probe/cookies", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_browser_cookies,
               NULL);
CRUX_WEB_ROUTE(slow, "/probe/slow/{ms}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_slow, NULL);
CRUX_WEB_ROUTE(static_body, "/probe/static/{size}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_static, NULL);
CRUX_WEB_ROUTE(page, "/probe/page", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_page, NULL);
CRUX_WEB_ROUTE(page_script, "/probe/page.js", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_page_script, NULL);
CRUX_WEB_ROUTE(fail, "/probe/fail/{skip}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_fail, NULL);
CRUX_WEB_ROUTE(other, "/probe/other", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_other, NULL);
CRUX_WEB_ROUTE(send_buffer, "/probe/send-buffer/{bytes}", CRUX_WEB_GET, CRUX_WEB_PUBLIC,
               on_send_buffer, NULL);
CRUX_WEB_ROUTE(stats, "/probe/stats", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_stats, NULL);
