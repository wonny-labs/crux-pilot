/* Request bodies, deadlines, release on disconnect and the response form. */
#include "web_test.h"

#include <string.h>

static fake_client_t *client;

static void fresh(void)
{
    web_test_setup(false);
    client = fake_connect();
    CHECK(client != NULL);
}

static crux_web_request_head_t head_with_length(fake_cookie_t cookie, bool chunked,
                                                const char *length)
{
    crux_web_request_head_t head;

    memset(&head, 0, sizeof(head));
    head.cookie = cookie;
    head.chunked = chunked;
    head.content_length = length;
    return head;
}

static void expect_closed_with(int status, const char *code)
{
    char expected[64];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    CHECK(client->reply.origin == FAKE_FROM_CORE);
    CHECK(client->reply.status == status);
    CHECK(strcmp(client->reply.body, expected) == 0);
    CHECK(client->reply.closed);
    CHECK(web_record.calls == 0u);
}

static void test_body_reassembled(void)
{
    static const char content[] = "{\"user\":\"admin\",\"password\":\"x\"}";
    size_t total = sizeof(content) - 1u;
    char length[8];

    fresh();
    snprintf(length, sizeof(length), "%zu", total);
    CHECK(fake_begin(client, CRUX_WEB_POST, "/api/v1/session",
                     head_with_length(NULL, false, length)));
    for (size_t index = 0; index + 1u < total; ++index) {
        fake_body(client, content + index, 1u, false);
        CHECK(web_record.calls == 0u);
    }
    fake_body(client, content + total - 1u, 1u, true);
    CHECK(client->reply.status == 200);
    CHECK(web_record.calls == 1u);
    CHECK(web_record.body_length == total);
    CHECK(memcmp(web_record.body, content, total) == 0);
}

static void test_body_final_with_data_and_empty(void)
{
    fresh();
    CHECK(fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "2")));
    fake_body(client, "ok", 2u, true);
    CHECK(web_record.body_length == 2u);
    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, NULL)));
    fake_body(client, NULL, 0u, true);
    CHECK(web_record.calls == 1u && web_record.body_length == 0u);
}

/* The limit is checked from Content-Length, before any byte is stored or a session is looked up. */
static void test_body_over_limit(void)
{
    fresh();
    CHECK(!fake_begin(client, CRUX_WEB_POST, "/api/v1/session",
                      head_with_length(NULL, false, "513")));
    expect_closed_with(413, "payload_too_large");
    fresh();
    CHECK(!fake_begin(client, CRUX_WEB_POST, "/api/v1/session/me/password",
                      head_with_length(web_admin_cookie, false, "257")));
    expect_closed_with(413, "payload_too_large");
    CHECK(web_auth_calls == 0u);
    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "512")));
    fresh();
    CHECK(!fake_begin(client, CRUX_WEB_PUT, "/api/v1/files/a/b",
                      head_with_length(web_admin_cookie, false, "99999999999999999999999")));
    expect_closed_with(413, "payload_too_large");
}

static void test_malformed_length(void)
{
    static const char *const lengths[] = {"", "-1", "1a", " 5", "0x10", "+1"};

    for (size_t index = 0; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        fresh();
        CHECK(!fake_begin(client, CRUX_WEB_POST, "/api/v1/session",
                          head_with_length(NULL, false, lengths[index])));
        expect_closed_with(400, "bad_request");
    }
}

/* The server hands the handler raw chunk framing, so a chunked body is refused. */
static void test_chunked_refused(void)
{
    fresh();
    CHECK(
        !fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, true, NULL)));
    expect_closed_with(400, "bad_request");
}

/* The server runs the callback once for each read of a GET's body, so a handler could run twice. */
static void test_body_on_method_without_one_refused(void)
{
    static const unsigned methods[] = {CRUX_WEB_GET, CRUX_WEB_DELETE};
    static const char *const urls[] = {"/api/v1/status", "/api/v1/session"};

    for (size_t index = 0; index < 2u; ++index) {
        fresh();
        CHECK(!fake_begin(client, methods[index], urls[index],
                          head_with_length(web_admin_cookie, false, "4")));
        expect_closed_with(400, "bad_request");
    }
    fresh();
    CHECK(fake_begin(client, CRUX_WEB_GET, "/api/v1/status",
                     head_with_length(web_admin_cookie, false, "0")));
}

/* The server passes everything in its buffer, so pipelined bytes would look like body. */
static void test_more_body_than_declared_refused(void)
{
    fresh();
    CHECK(fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "2")));
    fake_body(client, "okGET /x", 8u, true);
    expect_closed_with(400, "bad_request");
    fresh();
    CHECK(fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "4")));
    fake_body(client, "ok", 2u, true);
    expect_closed_with(400, "bad_request");
}

/* The server only asks for an upgrade after sending its own 426, and it runs the resource after. */
static void test_upgrade_refused(void)
{
    crux_web_request_head_t head = head_with_length(web_admin_cookie, false, NULL);

    fresh();
    head.upgrade = true;
    CHECK(!fake_begin(client, CRUX_WEB_GET, "/api/v1/status", head));
    CHECK(client->reply.origin == FAKE_NONE);
    CHECK(client->reply.closed);
    CHECK(web_record.calls == 0u);
}

/* The whole-request deadline: every chunk restarts the server's inactivity timer, and not this. */
static void test_deadline_closes_trickled_body(void)
{
    crux_web_stats_t stats;
    fake_client_t *other;
    uint64_t now = 1000u;

    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "100")));
    CHECK(fake_resource_held("/api/v1/session"));
    for (int step = 0; step < 4; ++step) {
        now += 1000u;
        web_test_clock_set(now);
        fake_body(client, "a", 1u, false);
        CHECK(client->open);
    }
    web_test_clock_set(1000u + WEB_TEST_DEADLINE_MS);
    fake_body(client, "a", 1u, false);
    CHECK(client->open);
    web_test_clock_set(1000u + WEB_TEST_DEADLINE_MS + 1u);
    fake_body(client, "a", 1u, false);
    CHECK(!client->open);
    CHECK(client->reply.origin == FAKE_NONE);
    CHECK(web_record.calls == 0u);
    CHECK(!fake_resource_held("/api/v1/session"));
    crux_web_stats_get(&stats);
    CHECK(stats.deadlines == 1u);
    other = fake_connect();
    CHECK(fake_request(other, "/api/v1/session", CRUX_WEB_POST, NULL)->status == 200);
}

/* The last chunk counts too, so a body that finishes late runs no handler. */
static void test_deadline_applies_to_last_chunk(void)
{
    fresh();
    CHECK(fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "2")));
    web_test_clock_set(1000u + WEB_TEST_DEADLINE_MS + 1u);
    fake_body(client, "ok", 2u, true);
    CHECK(!client->open);
    CHECK(web_record.calls == 0u);
}

static void test_disconnect_releases_route(void)
{
    fake_client_t *other;
    crux_web_stats_t stats;

    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "10")));
    fake_body(client, "ab", 2u, false);
    other = fake_connect();
    CHECK(!fake_begin(other, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "2")));
    CHECK(other->reply.origin == FAKE_FROM_SERVER && other->reply.status == 409);
    CHECK(other->reply.closed);
    fake_disconnect(client);
    CHECK(!fake_resource_held("/api/v1/session"));
    other = fake_connect();
    CHECK(fake_request(other, "/api/v1/session", CRUX_WEB_POST, NULL)->status == 200);
    CHECK(web_record.calls == 1u);
    crux_web_stats_get(&stats);
    CHECK(stats.aborted == 1u);
}

static void test_exchange_pool_bounded_and_reused(void)
{
    crux_web_exchange_t *held[8] = {NULL};
    size_t count = 0;

    fresh();
    while (count < 8u && (held[count] = crux_web_exchange_open()) != NULL) {
        ++count;
    }
    CHECK(count == 3u);
    CHECK(crux_web_exchange_open() == NULL);
    crux_web_exchange_close(held[1], true);
    CHECK(crux_web_exchange_open() == held[1]);
    for (size_t index = 0; index < count; ++index) {
        crux_web_exchange_close(held[index], true);
    }
}

/* The body may hold a password, so no copy stays after the request, however it ended. */
static void test_body_cleared(void)
{
    static const char secret[] = "correct-horse-secret";

    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "20")));
    fake_body(client, secret, 20u, true);
    CHECK(client->reply.status == 200);
    CHECK(!web_core_holds_bytes(secret, 20u));
    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "40")));
    fake_body(client, secret, 20u, false);
    CHECK(web_core_holds_bytes(secret, 20u));
    fake_disconnect(client);
    CHECK(!web_core_holds_bytes(secret, 20u));
    fresh();
    CHECK(
        fake_begin(client, CRUX_WEB_POST, "/api/v1/session", head_with_length(NULL, false, "40")));
    fake_body(client, secret, 20u, false);
    web_test_clock_set(1000u + WEB_TEST_DEADLINE_MS + 1u);
    fake_body(client, "x", 1u, false);
    CHECK(!web_core_holds_bytes(secret, 20u));
}

static void expect_security_headers(const fake_reply_t *reply)
{
    const char *csp = fake_reply_header(reply, "Content-Security-Policy");

    CHECK(csp != NULL && strcmp(csp, "default-src 'self'; frame-ancestors 'none'") == 0);
    CHECK(strcmp(fake_reply_header(reply, "X-Content-Type-Options"), "nosniff") == 0);
    CHECK(strcmp(fake_reply_header(reply, "Referrer-Policy"), "no-referrer") == 0);
    CHECK(strcmp(fake_reply_header(reply, "Cache-Control"), "no-store") == 0);
    CHECK(fake_reply_header(reply, "Content-Type") != NULL);
    CHECK(fake_reply_header(reply, "Strict-Transport-Security") == NULL);
    CHECK(fake_reply_header(reply, "Access-Control-Allow-Origin") == NULL);
}

static void test_every_core_response_has_security_headers(void)
{
    fresh();
    expect_security_headers(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie));
    fresh();
    expect_security_headers(fake_request(client, "/api/v1/status", CRUX_WEB_GET, NULL));
    fresh();
    expect_security_headers(
        fake_request(client, "/api/v1/status/x", CRUX_WEB_GET, web_admin_cookie));
    fresh();
    expect_security_headers(
        fake_request(client, "/api/v1/network", CRUX_WEB_GET, web_forced_cookie));
    fresh();
    CHECK(!fake_begin(client, CRUX_WEB_POST, "/api/v1/session",
                      head_with_length(NULL, false, "513")));
    expect_security_headers(&client->reply);
}

static void test_handler_without_response(void)
{
    fresh();
    web_record.mode = WEB_HANDLER_NO_RESPONSE;
    CHECK(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie)->status == 503);
    CHECK(strcmp(client->reply.body, "{\"code\":\"not_ready\"}") == 0);
    CHECK(fake_reply_header(&client->reply, "Retry-After") != NULL);
    fresh();
    web_record.mode = WEB_HANDLER_FAIL;
    CHECK(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie)->status == 503);
}

static void test_response_api_checks(void)
{
    size_t cookies = 0;

    fresh();
    web_record.mode = WEB_HANDLER_API_CHECKS;
    CHECK(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie)->status == 200);
    for (size_t index = 0; index < client->reply.header_count; ++index) {
        cookies += strcmp(client->reply.names[index], "Set-Cookie") == 0 ? 1u : 0u;
    }
    CHECK(cookies == 2u);
    CHECK(strcmp(client->reply.body, "{\"ok\":true}") == 0);
}

static void test_handler_limit_counted(void)
{
    crux_web_stats_t stats;

    fresh();
    web_record.mode = WEB_HANDLER_SLOW;
    CHECK(fake_request(client, "/api/v1/status", CRUX_WEB_GET, web_admin_cookie)->status == 200);
    crux_web_stats_get(&stats);
    CHECK(stats.handler_overruns == 1u);
    CHECK(stats.handler_max_ms == WEB_TEST_HANDLER_LIMIT_MS + 50u);
}

static void test_exchange_finished_after_response(void)
{
    crux_web_exchange_t *exchange;
    crux_web_request_head_t head = head_with_length(web_admin_cookie, false, NULL);
    crux_web_response_t response;

    fresh();
    head.method = CRUX_WEB_GET;
    head.url = "/api/v1/status";
    exchange = crux_web_exchange_open();
    CHECK(!crux_web_exchange_finished(exchange));
    CHECK(crux_web_exchange_start(exchange, &head, &response) == CRUX_WEB_STEP_MORE);
    CHECK(!crux_web_exchange_finished(exchange));
    CHECK(crux_web_exchange_data(exchange, NULL, 0u, true, &response) == CRUX_WEB_STEP_RESPOND);
    CHECK(crux_web_exchange_finished(exchange));
    /* A further call is a fault: nothing runs twice. */
    CHECK(crux_web_exchange_data(exchange, NULL, 0u, true, &response) == CRUX_WEB_STEP_CLOSE);
    CHECK(web_record.calls == 1u);
    crux_web_exchange_close(exchange, false);
}

int main(void)
{
    test_body_reassembled();
    test_body_final_with_data_and_empty();
    test_body_over_limit();
    test_malformed_length();
    test_chunked_refused();
    test_body_on_method_without_one_refused();
    test_more_body_than_declared_refused();
    test_upgrade_refused();
    test_deadline_closes_trickled_body();
    test_deadline_applies_to_last_chunk();
    test_disconnect_releases_route();
    test_exchange_pool_bounded_and_reused();
    test_body_cleared();
    test_every_core_response_has_security_headers();
    test_handler_without_response();
    test_response_api_checks();
    test_handler_limit_counted();
    test_exchange_finished_after_response();
    return access_test_finish("web requests");
}
