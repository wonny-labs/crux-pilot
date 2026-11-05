/* Access enforcement before a handler runs, with the real Access core and real logins. */
#include "web_internal.h"
#include "web_test.h"

#include <string.h>

static fake_client_t *client;

static void fresh(bool no_port)
{
    web_test_setup(no_port);
    client = fake_connect();
    CHECK(client != NULL);
}

static void expect_error(const fake_reply_t *reply, int status, const char *code)
{
    char expected[64];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == status);
    CHECK(strcmp(reply->body, expected) == 0);
    CHECK(strcmp(fake_reply_header(reply, "Content-Type"), "application/json") == 0);
    CHECK(web_record.calls == 0u);
}

static void test_public_route_needs_no_session(void)
{
    fresh(false);
    web_auth_force = CRUX_WEB_AUTH_NO_SESSION;
    CHECK(fake_request(client, "/api/v1/session/attempt/abc", CRUX_WEB_GET, NULL)->status == 200);
    CHECK(web_auth_calls == 0u);
    CHECK(web_record.calls == 1u);
    CHECK(web_record.session.value == 0u);
}

static void test_session_route(void)
{
    fresh(false);
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, NULL), 401, "unauthorized");
    fresh(false);
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, "sid=12345"), 401,
                 "unauthorized");
    fresh(false);
    CHECK(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_viewer_cookie)->status == 200);
    CHECK(web_record.session.value == web_viewer.value);
}

static void test_session_route_after_logout(void)
{
    crux_access_session_t ended;

    fresh(false);
    ended = web_viewer;
    crux_access_logout(&ended);
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_viewer_cookie), 401,
                 "unauthorized");
}

static void test_permission_route(void)
{
    size_t events_before;

    fresh(false);
    expect_error(fake_request(client, "/api/v1/network", CRUX_WEB_PUT, NULL), 401, "unauthorized");
    fresh(false);
    CHECK(fake_request(client, "/api/v1/network", CRUX_WEB_GET, web_viewer_cookie)->status == 200);
    CHECK(strcmp(web_record.route, "network_read") == 0);
    fresh(false);
    events_before = access_fake_event_count();
    /* The real check runs, so a denial leaves its event. */
    {
        crux_web_request_head_t head;
        memset(&head, 0, sizeof(head));
        head.cookie = web_viewer_cookie;
        head.content_length = "2";
        CHECK(!fake_begin(client, CRUX_WEB_PUT, "/api/v1/network", head));
    }
    expect_error(&client->reply, 403, "permission_denied");
    CHECK(access_fake_event_count() == events_before + 1u);
    fresh(false);
    CHECK(fake_request(client, "/api/v1/network", CRUX_WEB_GET, web_admin_cookie)->status == 200);
}

/* A session under a forced change reaches session routes, and permission routes return 403. */
static void test_forced_password_change(void)
{
    fresh(false);
    CHECK(fake_request(client, "/api/v1/session/me", CRUX_WEB_GET, web_forced_cookie)->status ==
          200);
    fresh(false);
    expect_error(fake_request(client, "/api/v1/network", CRUX_WEB_GET, web_forced_cookie), 403,
                 "permission_denied");
}

static void test_session_lookup_outcomes(void)
{
    fresh(false);
    web_auth_force = CRUX_WEB_AUTH_CSRF_REJECTED;
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie), 403,
                 "csrf_rejected");
    fresh(false);
    web_auth_force = CRUX_WEB_AUTH_ORIGIN_REJECTED;
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie), 403,
                 "origin_rejected");
    fresh(false);
    web_auth_force = CRUX_WEB_AUTH_NOT_READY;
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie), 503,
                 "not_ready");
    CHECK(fake_reply_header(&client->reply, "Retry-After") != NULL);
}

static void test_no_session_port_fails_closed(void)
{
    fresh(true);
    expect_error(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie), 503,
                 "not_ready");
    fresh(true);
    CHECK(fake_request(client, "/api/v1/session/attempt/x", CRUX_WEB_GET, NULL)->status == 200);
}

/* A dropped header is rejected, and is never treated as having no session. */
static void test_dropped_headers_rejected(void)
{
    crux_web_request_head_t head;
    static const struct {
        unsigned method;
        const char *url;
    } routes[] = {
        {CRUX_WEB_GET, "/api/v1/status"},
        {CRUX_WEB_POST, "/api/v1/session"},
        {CRUX_WEB_GET, "/api/v1/session/attempt/x"},
    };

    for (size_t index = 0; index < sizeof(routes) / sizeof(routes[0]); ++index) {
        fresh(false);
        memset(&head, 0, sizeof(head));
        head.cookie = web_admin_cookie;
        head.headers_incomplete = true;
        CHECK(!fake_begin(client, routes[index].method, routes[index].url, head));
        expect_error(&client->reply, 400, "bad_request");
        CHECK(web_auth_calls == 0u);
        CHECK(client->reply.closed);
    }
    fresh(false);
    memset(&head, 0, sizeof(head));
    head.cookie = web_admin_cookie;
    head.duplicate_header = true;
    CHECK(!fake_begin(client, CRUX_WEB_GET, "/api/v1/status", head));
    expect_error(&client->reply, 400, "bad_request");
    CHECK(web_auth_calls == 0u);
}

/* A route defined by hand, with no access class, has no way to run. */
static crux_web_result_t never_runs(crux_web_request_t *request, crux_access_session_t session,
                                    void *context)
{
    (void)request;
    (void)session;
    (void)context;
    ++web_record.calls;
    return CRUX_WEB_OK;
}

extern const crux_web_route_t *crux_web_core_test_route;
static const crux_web_route_t hand_made[] = {
    {"/hand", CRUX_WEB_GET, NULL, never_runs, NULL, 0u},
    {"/hand", CRUX_WEB_GET, "", never_runs, NULL, 0u},
};

static void test_route_without_access_class_never_runs(void)
{
    crux_web_response_t response;
    crux_web_request_head_t head;

    for (size_t index = 0; index < 2u; ++index) {
        crux_web_exchange_t *exchange;

        fresh(false);
        memset(&head, 0, sizeof(head));
        head.method = CRUX_WEB_GET;
        head.url = "/hand";
        head.cookie = web_admin_cookie;
        crux_web_core_test_route = &hand_made[index];
        exchange = crux_web_exchange_open();
        CHECK(exchange != NULL);
        CHECK(crux_web_exchange_start(exchange, &head, &response) == CRUX_WEB_STEP_RESPOND);
        CHECK(response.status == 403);
        CHECK(web_record.calls == 0u);
        crux_web_exchange_close(exchange, false);
        crux_web_core_test_route = NULL;
    }
}

/* What a session may do can change while a long body arrives. */
static void test_access_checked_again_before_handler(void)
{
    crux_web_request_head_t head;
    crux_access_session_t ended;

    fresh(false);
    memset(&head, 0, sizeof(head));
    head.cookie = web_admin_cookie;
    head.content_length = "4";
    CHECK(fake_begin(client, CRUX_WEB_PUT, "/api/v1/network", head));
    fake_body(client, "ab", 2u, false);
    ended = web_admin;
    crux_access_logout(&ended);
    fake_body(client, "cd", 2u, true);
    CHECK(client->reply.status == 401);
    CHECK(web_record.calls == 0u);
}

/* The one check a handler itself needs: it runs with the session the core resolved. */
static void test_handler_gets_session(void)
{
    fresh(false);
    CHECK(fake_request(client, "/api/v1/session/me", CRUX_WEB_GET, web_viewer_cookie)->status ==
          200);
    CHECK(web_record.session.value == web_viewer.value);
    CHECK(crux_access_check(web_record.session, "network.read") == CRUX_ACCESS_OK);
    CHECK(crux_access_check(web_record.session, "network.write") == CRUX_ACCESS_ERR_DENIED);
}

/* What the core checks of the table at start: every field, and what the build read against it. */
static void test_route_table_checked(void)
{
    static const crux_web_route_t good = {"/a", CRUX_WEB_GET, "x.y", never_runs, NULL, 16u};
    static const crux_web_route_t bad[] = {
        {NULL, CRUX_WEB_GET, "x.y", never_runs, NULL, 16u},
        {"a", CRUX_WEB_GET, "x.y", never_runs, NULL, 16u},
        {"/a", 0u, "x.y", never_runs, NULL, 16u},
        {"/a", 0x100u, "x.y", never_runs, NULL, 16u},
        {"/a", CRUX_WEB_GET, NULL, never_runs, NULL, 16u},
        {"/a", CRUX_WEB_GET, "", never_runs, NULL, 16u},
        {"/a", CRUX_WEB_GET, "x.y", NULL, NULL, 16u},
        {"/a", CRUX_WEB_GET, "x.y", never_runs, NULL, CRUX_WEB_BODY_BUFFER + 1u},
        /* Well formed, but not what the build read. */
        {"/b", CRUX_WEB_GET, "x.y", never_runs, NULL, 16u},
        {"/a", CRUX_WEB_PUT, "x.y", never_runs, NULL, 16u},
        {"/a", CRUX_WEB_GET, CRUX_WEB_PUBLIC, never_runs, NULL, 16u},
    };
    static const crux_web_route_t expected = {"/a", CRUX_WEB_GET, "x.y", NULL, NULL, 0u};
    const crux_web_route_t *table[1] = {&good};

    CHECK(crux_web_routes_check(table, &expected, 1u));
    for (size_t index = 0; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        table[0] = &bad[index];
        CHECK(!crux_web_routes_check(table, &expected, 1u));
    }
    /* The table this image linked is what its build read. */
    CHECK(crux_web_routes_valid());
}

int main(void)
{
    test_public_route_needs_no_session();
    test_session_route();
    test_session_route_after_logout();
    test_permission_route();
    test_forced_password_change();
    test_session_lookup_outcomes();
    test_no_session_port_fails_closed();
    test_dropped_headers_rejected();
    test_route_without_access_class_never_runs();
    test_access_checked_again_before_handler();
    test_handler_gets_session();
    test_route_table_checked();
    return access_test_finish("web enforcement");
}
