/* The route table of the Host suites: the API's own shape plus deeper and parameter neighbours,
 * with one recording handler. The generator reads this file, as it reads a project's. */
#include "web_port.h"
#include "web_test.h"

#include <crux/web.h>
#include <stdint.h>
#include <string.h>

static crux_web_result_t on_route(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    size_t transferred = 0;

    ++web_record.calls;
    web_record.route = (const char *)context;
    web_record.session = session;
    for (size_t index = 0; index < 2u; ++index) {
        web_record.params[index][0] = '\0';
        (void)crux_web_path_param(request, index, web_record.params[index],
                                  sizeof(web_record.params[index]));
    }
    (void)crux_web_query(request, web_record.query, sizeof(web_record.query));
    web_record.body_length = 0;
    do {
        CHECK(crux_web_read_body(request, web_record.body + web_record.body_length,
                                 sizeof(web_record.body) - web_record.body_length,
                                 &transferred) == CRUX_WEB_OK);
        web_record.body_length += transferred;
    } while (transferred != 0u);
    snprintf(web_record.content_type, sizeof(web_record.content_type), "%s",
             crux_web_content_type(request) != NULL ? crux_web_content_type(request) : "");

    switch (web_record.mode) {
    case WEB_HANDLER_NO_RESPONSE:
        return CRUX_WEB_OK;
    case WEB_HANDLER_FAIL:
        return CRUX_WEB_ERR_FAILED;
    case WEB_HANDLER_SLOW:
        web_test_clock_set(crux_web_port_now_ms_for_tests() + WEB_TEST_HANDLER_LIMIT_MS + 50u);
        break;
    case WEB_HANDLER_API_CHECKS: {
        char small[2];

        CHECK(crux_web_path_param(request, 7u, small, sizeof(small)) == CRUX_WEB_ERR_NOT_FOUND);
        CHECK(crux_web_add_header(request, "X-Bad\r\nInjected", "x") ==
              CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_add_header(request, "X-Note", "a\r\nInjected: x") ==
              CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_add_header(request, "Transfer-Encoding", "identity") ==
              CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_add_header(request, "content-security-policy", "default-src *") ==
              CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_add_header(request, "Set-Cookie", "crux_session=x; Secure") == CRUX_WEB_OK);
        CHECK(crux_web_add_header(request, "Set-Cookie", "XSRF-TOKEN=y; Secure") == CRUX_WEB_OK);
        CHECK(crux_web_respond(request, 204, NULL, NULL, 0u) == CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_respond(request, 99, NULL, NULL, 0u) == CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_respond(request, 200, "text/plain\r\nX: y", "x", 1u) ==
              CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_respond_error(request, 400, "Not A Code") == CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_respond(request, 200, NULL, NULL, SIZE_MAX) == CRUX_WEB_ERR_TOO_LARGE);
        CHECK(crux_web_respond(request, 200, NULL, "{\"ok\":true}", 11u) == CRUX_WEB_OK);
        CHECK(crux_web_respond(request, 200, NULL, "{}", 2u) == CRUX_WEB_ERR_ALREADY_RESPONDED);
        return CRUX_WEB_OK;
    }
    case WEB_HANDLER_OK:
        break;
    }
    return crux_web_respond(request, 200, NULL, "{\"ok\":true}", 11u);
}

/* The two public routes of the API. Two routes share each of these paths: its resource merges them.
 */
CRUX_WEB_ROUTE(login, "/api/v1/session", CRUX_WEB_POST, CRUX_WEB_PUBLIC, on_route, "login");
CRUX_WEB_ROUTE(logout, "/api/v1/session", CRUX_WEB_DELETE, CRUX_WEB_SESSION, on_route, "logout");
CRUX_WEB_ROUTE(attempt, "/api/v1/session/attempt/{id}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_route,
               "attempt");
CRUX_WEB_ROUTE(me, "/api/v1/session/me", CRUX_WEB_GET, CRUX_WEB_SESSION, on_route, "me");
CRUX_WEB_ROUTE_BODY(password, "/api/v1/session/me/password", CRUX_WEB_POST, CRUX_WEB_SESSION,
                    on_route, "password", 256u);
CRUX_WEB_ROUTE(status, "/api/v1/status", CRUX_WEB_GET, CRUX_WEB_SESSION, on_route, "status");
CRUX_WEB_ROUTE(network_read, "/api/v1/network", CRUX_WEB_GET, "network.read", on_route,
               "network_read");
CRUX_WEB_ROUTE(network_write, "/api/v1/network", CRUX_WEB_PUT, "network.write", on_route,
               "network_write");
CRUX_WEB_ROUTE(file_list, "/api/v1/files/{area}", CRUX_WEB_GET, CRUX_WEB_SESSION, on_route,
               "file_list");
CRUX_WEB_ROUTE_BODY(file_item, "/api/v1/files/{area}/{name}",
                    CRUX_WEB_GET | CRUX_WEB_PUT | CRUX_WEB_DELETE, CRUX_WEB_SESSION, on_route,
                    "file_item", CRUX_WEB_BODY_BUFFER);
CRUX_WEB_ROUTE(config_save, "/api/v1/config/save", CRUX_WEB_POST, "config.save", on_route,
               "config_save");
CRUX_WEB_ROUTE(config_item, "/api/v1/config/{name}", CRUX_WEB_GET | CRUX_WEB_PUT, CRUX_WEB_SESSION,
               on_route, "config_item");
