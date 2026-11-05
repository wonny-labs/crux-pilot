/* The two-request login: queueing, the poll and its header rule, cookies, the token source, and
 * replies that do not say whether a user exists. */
#include "session_test.h"

#include <stdlib.h>

bool web_core_holds_bytes(const void *bytes, size_t length);
bool web_session_attempts_hold_bytes(const void *bytes, size_t length);
bool web_session_routes_hold_bytes(const void *bytes, size_t length);

static void base64url(const uint8_t *bytes, char *text)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint8_t padded[CRUX_WEB_SESSION_TOKEN_BYTES + 2u] = {0};
    size_t out = 0;

    memcpy(padded, bytes, CRUX_WEB_SESSION_TOKEN_BYTES);
    for (size_t index = 0; index < CRUX_WEB_SESSION_TOKEN_BYTES; index += 3u) {
        uint32_t group = ((uint32_t)padded[index] << 16) | ((uint32_t)padded[index + 1u] << 8) |
                         padded[index + 2u];
        for (int shift = 18; shift >= 0 && out < CRUX_WEB_SESSION_TOKEN_TEXT; shift -= 6) {
            text[out++] = alphabet[(group >> shift) & 0x3fu];
        }
    }
    text[out] = '\0';
}

static bool base64url_only(const char *text)
{
    return strspn(text, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_") ==
           strlen(text);
}

static void expect_error(const fake_reply_t *reply, int status, const char *code)
{
    char expected[64];
    const char *cookies[2];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == status);
    CHECK(strcmp(reply->body, expected) == 0);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 2u) == 0u);
}

/* POST queues and closes, a poll before the worker says pending, and the poll after it hands out
 * the session once. */
static void test_two_request_login(void)
{
    char id[80];
    const fake_reply_t *reply;
    const char *cookies[4];
    session_jar_t jar;

    session_test_setup();
    reply = session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id));
    CHECK(reply->status == 202);
    CHECK(strlen(id) == CRUX_WEB_SESSION_TOKEN_TEXT && base64url_only(id));
    CHECK(reply->closed);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 4u) == 0u);
    CHECK(access_fake_derive_count() == 0u);

    reply = session_poll(id);
    CHECK(reply->status == 200 && strcmp(reply->body, "{\"state\":\"pending\"}") == 0);
    CHECK(!reply->closed);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 4u) == 0u);

    CHECK(web_session_work_once());
    CHECK(!web_session_work_once());
    CHECK(access_fake_derive_count() == 1u);
    reply = session_poll(id);
    CHECK(reply->status == 200 && strcmp(reply->body, "{\"state\":\"success\"}") == 0);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 4u) == 2u);
    CHECK(web_session_table_count() == 1u);

    /* A result is collected once. */
    expect_error(session_poll(id), 404, "not_found");

    reply = session_login("viewer", SESSION_TEST_PASSWORD, &jar);
    CHECK(reply->status == 200);
    reply = session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL);
    CHECK(reply->status == 200);
    CHECK(strcmp(reply->body,
                 "{\"user\":\"viewer\",\"role\":\"viewer\",\"must_change_password\":false}") == 0);
}

static void test_cookie_attributes(void)
{
    char id[80];
    const fake_reply_t *reply;
    const char *cookies[4];
    char session_value[64];
    char xsrf_value[64];
    char expected[160];

    session_test_setup();
    CHECK(session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id))->status == 202);
    CHECK(web_session_work_once());
    reply = session_poll(id);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 4u) == 2u);
    CHECK(sscanf(cookies[0], "crux_session=%63[^;]", session_value) == 1);
    CHECK(sscanf(cookies[1], "XSRF-TOKEN=%63[^;]", xsrf_value) == 1);
    snprintf(expected, sizeof(expected),
             "crux_session=%s; Secure; HttpOnly; SameSite=Strict; Path=/", session_value);
    CHECK(strcmp(cookies[0], expected) == 0);
    snprintf(expected, sizeof(expected), "XSRF-TOKEN=%s; Secure; SameSite=Strict; Path=/",
             xsrf_value);
    CHECK(strcmp(cookies[1], expected) == 0);
    /* No % and no quotes, because Angular decodes the cookie and the Crux client sends it raw. */
    CHECK(strlen(session_value) == CRUX_WEB_SESSION_TOKEN_TEXT && base64url_only(session_value));
    CHECK(strlen(xsrf_value) == CRUX_WEB_SESSION_TOKEN_TEXT && base64url_only(xsrf_value));
    CHECK(strchr(cookies[1], '%') == NULL && strchr(cookies[1], '"') == NULL);
    /* Independent of each other and of the attempt identifier. */
    CHECK(strcmp(session_value, xsrf_value) != 0);
    CHECK(strcmp(session_value, id) != 0 && strcmp(xsrf_value, id) != 0);
    /* No persistent expiry, so they end with the browser. */
    CHECK(strstr(cookies[0], "Expires") == NULL && strstr(cookies[0], "Max-Age") == NULL);
    CHECK(strstr(cookies[1], "Expires") == NULL && strstr(cookies[1], "Max-Age") == NULL);
}

/* The identifier, the session token and the CSRF token are each one draw of 256 bits from the one
 * random port. */
static void test_identifiers_come_from_the_token_source(void)
{
    char id[80];
    char expected[64];
    const fake_reply_t *reply;
    const char *cookies[4];
    char xsrf_value[64];
    unsigned calls;

    session_test_setup();
    calls = session_fake_random_calls();
    reply = session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id));
    CHECK(reply->status == 202);
    CHECK(session_fake_random_calls() == calls + 1u);
    base64url(session_fake_random_last(), expected);
    CHECK(strcmp(id, expected) == 0);

    CHECK(web_session_work_once());
    CHECK(session_fake_random_calls() == calls + 3u);
    reply = session_poll(id);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 4u) == 2u);
    CHECK(sscanf(cookies[1], "XSRF-TOKEN=%63[^;]", xsrf_value) == 1);
    base64url(session_fake_random_last(), expected);
    CHECK(strcmp(xsrf_value, expected) == 0);
    CHECK(CRUX_WEB_SESSION_TOKEN_BYTES * 8u >= 128u);
}

/* A poll without X-Crux-Request: 1 gets 403 csrf_rejected, sets no cookie and leaves the attempt as
 * it was, before and after the worker has finished. */
static void test_poll_requires_request_header(void)
{
    static const char *const wrong[] = {NULL, "", "0", "true", "11", "1 ", " 1", "yes"};
    char id[80];
    char url[160];
    const fake_reply_t *reply;
    session_request_t request = {.method = CRUX_WEB_GET, .url = url, .host = SESSION_TEST_HOST};

    session_test_setup();
    CHECK(session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id))->status == 202);
    snprintf(url, sizeof(url), "/api/v1/session/attempt/%s", id);
    for (size_t index = 0; index < sizeof(wrong) / sizeof(wrong[0]); ++index) {
        request.crux_request = wrong[index];
        expect_error(session_send(&request), 403, "csrf_rejected");
    }
    CHECK(web_session_work_once());
    for (size_t index = 0; index < sizeof(wrong) / sizeof(wrong[0]); ++index) {
        request.crux_request = wrong[index];
        expect_error(session_send(&request), 403, "csrf_rejected");
    }
    CHECK(web_session_table_count() == 0u);
    /* A browser navigation with the identifier can neither collect the result nor end it. */
    reply = session_poll(id);
    CHECK(reply->status == 200 && strcmp(reply->body, "{\"state\":\"success\"}") == 0);
    CHECK(web_session_table_count() == 1u);
    /* An unknown identifier without the header is refused for the header, and says nothing. */
    request.crux_request = NULL;
    snprintf(url, sizeof(url), "/api/v1/session/attempt/%s", "x");
    expect_error(session_send(&request), 403, "csrf_rejected");
}

static void test_unknown_and_malformed_identifiers(void)
{
    char id[80];

    session_test_setup();
    expect_error(session_poll("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), 404, "not_found");
    expect_error(session_poll("short"), 404, "not_found");
    expect_error(session_poll("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"), 404, "not_found");
    (void)session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id));
    /* A prefix of a live identifier is not it. */
    id[CRUX_WEB_SESSION_TOKEN_TEXT - 1u] = '\0';
    expect_error(session_poll(id), 404, "not_found");
}

/* Every rejected login gives the same replies, byte for byte. */
static void expect_rejected(const char *user, const char *password)
{
    char id[80];
    const fake_reply_t *reply;
    const char *cookies[2];

    reply = session_post_login(user, password, id, sizeof(id));
    CHECK(reply->status == 202 && strlen(id) == CRUX_WEB_SESSION_TOKEN_TEXT);
    CHECK(reply->header_count == 5u);
    CHECK(web_session_work_once());
    reply = session_poll(id);
    CHECK(reply->status == 200);
    CHECK(strcmp(reply->body, "{\"state\":\"rejected\"}") == 0);
    CHECK(reply->header_count == 5u);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 2u) == 0u);
    CHECK(web_session_table_count() == 0u);
}

static void test_replies_do_not_reveal_a_user(void)
{
    char long_password[300];

    memset(long_password, 'p', sizeof(long_password) - 1u);
    long_password[sizeof(long_password) - 1u] = '\0';

    session_test_setup();
    expect_rejected("admin", "wrong password here");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGIN_FAILURE) == 1u);
    session_test_setup();
    expect_rejected("nobody", "wrong password here");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGIN_FAILURE) == 1u);
    session_test_setup();
    CHECK(crux_access_account_disable(session_admin, "viewer") == CRUX_ACCESS_OK);
    expect_rejected("viewer", SESSION_TEST_PASSWORD);
    session_test_setup();
    expect_rejected("Not A Name!", SESSION_TEST_PASSWORD);
    session_test_setup();
    expect_rejected("abcdefghijklmnopqrstuvwxyz0123456789", SESSION_TEST_PASSWORD);
    session_test_setup();
    expect_rejected("ad\\u0000min", SESSION_TEST_PASSWORD);
    session_test_setup();
    expect_rejected("admin", long_password);
    /* An unusable credential store looks the same as a wrong password. */
    session_test_setup();
    access_fake_derive_fail(true);
    expect_rejected("admin", SESSION_TEST_PASSWORD);
    access_fake_derive_fail(false);
}

static void test_delay_window_is_429(void)
{
    char id[80];
    const fake_reply_t *reply = NULL;
    const char *retry[2];
    bool delayed = false;

    session_test_setup();
    for (unsigned attempt = 0; attempt < 10u && !delayed; ++attempt) {
        CHECK(session_post_login("admin", "wrong password here", id, sizeof(id))->status == 202);
        CHECK(web_session_work_once());
        reply = session_poll(id);
        delayed = reply->status == 429;
    }
    CHECK(delayed);
    expect_error(reply, 429, "retry_later");
    CHECK(session_reply_headers(reply, "Retry-After", retry, 2u) == 1u);
    CHECK(atoi(retry[0]) >= 1);
    /* The attempt has ended. */
    expect_error(session_poll(id), 404, "not_found");
}

/* One attempt is queued or running at a time, and the table refuses rather than drop a result. */
static void test_busy_and_full(void)
{
    char id[80];
    char other[80];
    char ids[CRUX_WEB_SESSION_ATTEMPTS][80];
    const fake_reply_t *reply;
    const char *retry[2];
    crux_web_session_stats_t stats;

    session_test_setup();
    CHECK(session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id))->status == 202);
    reply = session_post_login("viewer", SESSION_TEST_PASSWORD, other, sizeof(other));
    expect_error(reply, 503, "busy");
    CHECK(reply->closed);
    CHECK(session_reply_headers(reply, "Retry-After", retry, 2u) == 1u);
    CHECK(web_session_work_once());
    CHECK(session_poll(id)->status == 200);

    session_test_setup();
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        CHECK(session_post_login("nobody", "wrong password here", ids[index], sizeof(ids[index]))
                  ->status == 202);
        CHECK(web_session_work_once());
    }
    expect_error(session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id)), 503, "busy");
    /* Results expire on their own, and then there is room. */
    session_clock_advance(CRUX_WEB_SESSION_ATTEMPT_TTL_MS + 1u);
    CHECK(session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id))->status == 202);
    for (size_t index = 0; index < CRUX_WEB_SESSION_ATTEMPTS; ++index) {
        expect_error(session_poll(ids[index]), 404, "not_found");
    }
    crux_web_session_stats_get(&stats);
    CHECK(stats.expired == CRUX_WEB_SESSION_ATTEMPTS);
    CHECK(stats.busy == 1u);
}

/* A success that nobody collects is logged out when it expires. */
static void test_uncollected_session_is_logged_out(void)
{
    char id[80];

    session_test_setup();
    CHECK(session_post_login("viewer", SESSION_TEST_PASSWORD, id, sizeof(id))->status == 202);
    CHECK(web_session_work_once());
    access_fake_events_clear();
    session_clock_advance(CRUX_WEB_SESSION_ATTEMPT_TTL_MS + 1u);
    web_session_attempts_expire();
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGOUT) == 1u);
    expect_error(session_poll(id), 404, "not_found");
}

/* Origin is checked on login too, compared whole, before anything is queued. */
static void test_login_origin(void)
{
    static const struct {
        const char *origin;
        const char *host;
    } cases[] = {
        {NULL, SESSION_TEST_HOST},
        {"", SESSION_TEST_HOST},
        {"null", SESSION_TEST_HOST},
        {"https://evil.test", SESSION_TEST_HOST},
        {"https://device.test.evil.test", SESSION_TEST_HOST},
        {"https://device.tes", SESSION_TEST_HOST},
        {"https://device.test/", SESSION_TEST_HOST},
        {"https://device.test:443", SESSION_TEST_HOST},
        {"http://device.test", SESSION_TEST_HOST},
        {"HTTPS://device.test", SESSION_TEST_HOST},
        {"https://DEVICE.test", SESSION_TEST_HOST},
        {SESSION_TEST_ORIGIN, NULL},
        {SESSION_TEST_ORIGIN, ""},
        {"https://evil.test/x", "evil.test/x"},
        {"https://a b", "a b"},
    };
    session_request_t request = {.method = CRUX_WEB_POST,
                                 .url = "/api/v1/session",
                                 .content_type = "application/json",
                                 .body = "{\"user\":\"admin\",\"password\":\"" SESSION_TEST_PASSWORD
                                         "\"}"};
    const fake_reply_t *reply;
    unsigned calls;

    session_test_setup();
    calls = session_fake_random_calls();
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        request.origin = cases[index].origin;
        request.host = cases[index].host;
        reply = session_send(&request);
        expect_error(reply, 403, "origin_rejected");
        CHECK(reply->closed);
    }
    CHECK(session_fake_random_calls() == calls);
    CHECK(!web_session_work_once());
    /* With the right Origin, and a Host with a port. */
    request.origin = "https://device.test:8443";
    request.host = "device.test:8443";
    CHECK(session_send(&request)->status == 202);
}

static void test_login_body_rules(void)
{
    static const char *const bodies[] = {
        "{\"user\":\"admin\"}",
        "{\"user\":\"admin\",\"password\":\"x\",\"remember\":\"yes\"}",
        "{\"user\":\"admin\",\"password\":7}",
        "user=admin&password=x",
        "{\"user\":\"admin\",\"user\":\"viewer\",\"password\":\"x\"}",
    };
    session_request_t request = {.method = CRUX_WEB_POST,
                                 .url = "/api/v1/session",
                                 .origin = SESSION_TEST_ORIGIN,
                                 .host = SESSION_TEST_HOST,
                                 .content_type = "application/json"};
    char big[1100];

    session_test_setup();
    for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        request.body = bodies[index];
        expect_error(session_send(&request), 400, "bad_request");
    }
    request.body = "{\"user\":\"admin\",\"password\":\"x\"}";
    request.content_type = "text/plain";
    expect_error(session_send(&request), 400, "bad_request");
    request.content_type = NULL;
    expect_error(session_send(&request), 400, "bad_request");
    request.content_type = "application/jsonp";
    expect_error(session_send(&request), 400, "bad_request");
    request.content_type = "Application/JSON; charset=utf-8";
    CHECK(session_send(&request)->status == 202);
    CHECK(web_session_work_once());

    session_test_setup();
    memset(big, ' ', sizeof(big) - 1u);
    big[sizeof(big) - 1u] = '\0';
    request.content_type = "application/json";
    request.body = big;
    expect_error(session_send(&request), 413, "payload_too_large");
    CHECK(!web_session_work_once());
}

/* No copy of the password stays in the core, the attempt table or the handlers' buffers, and the
 * connection closes so that the server clears its own. */
static void test_password_does_not_stay(void)
{
    static const char marker[] = "unique-marker-password-41";
    char id[80];
    const fake_reply_t *reply;

    session_test_setup();
    reply = session_post_login("admin", marker, id, sizeof(id));
    CHECK(reply->status == 202 && reply->closed);
    CHECK(!web_core_holds_bytes(marker, strlen(marker)));
    CHECK(!web_session_routes_hold_bytes(marker, strlen(marker)));
    CHECK(web_session_attempts_hold_bytes(marker, strlen(marker)));
    CHECK(web_session_work_once());
    CHECK(!web_session_attempts_hold_bytes(marker, strlen(marker)));
    CHECK(session_poll(id)->status == 200);
}

static void test_random_failures(void)
{
    char id[80];

    session_test_setup();
    session_fake_random_fail(true);
    expect_error(session_post_login("admin", SESSION_TEST_PASSWORD, id, sizeof(id)), 503, "busy");
    session_fake_random_fail(false);

    /* Tokens drawn after a correct password: the session is ended and the client told busy. */
    session_test_setup();
    CHECK(session_post_login("viewer", SESSION_TEST_PASSWORD, id, sizeof(id))->status == 202);
    session_fake_random_fail(true);
    access_fake_events_clear();
    CHECK(web_session_work_once());
    session_fake_random_fail(false);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGOUT) == 1u);
    expect_error(session_poll(id), 503, "busy");
    CHECK(web_session_table_count() == 0u);
}

/* With CRUX_WEB_SESSION_MAX at 3, a fourth live session cannot be handed out, and the login it
 * made is ended. An ended session's slot is reused. */
static void test_full_session_table(void)
{
    session_jar_t jars[3];
    const fake_reply_t *reply;

    session_test_setup();
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &jars[0])->status == 200);
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jars[1])->status == 200);
    CHECK(session_login("forced", SESSION_TEST_PASSWORD, &jars[2])->status == 200);
    access_fake_events_clear();
    reply = session_login("viewer", SESSION_TEST_PASSWORD, NULL);
    expect_error(reply, 503, "busy");
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_LOGOUT) == 1u);
    CHECK(session_with(&jars[1], CRUX_WEB_DELETE, "/api/v1/session", NULL)->status == 200);
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, NULL)->status == 200);
    /* A session that crux_access ended, with no request since, is cleared when a slot is needed. */
    CHECK(web_session_table_count() == 3u);
    CHECK(crux_access_account_disable(session_admin, "forced") == CRUX_ACCESS_OK);
    CHECK(web_session_table_count() == 3u);
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, NULL)->status == 200);
    CHECK(web_session_table_count() == 3u);
}

int main(void)
{
    test_two_request_login();
    test_cookie_attributes();
    test_identifiers_come_from_the_token_source();
    test_poll_requires_request_header();
    test_unknown_and_malformed_identifiers();
    test_replies_do_not_reveal_a_user();
    test_delay_window_is_429();
    test_busy_and_full();
    test_uncollected_session_is_logged_out();
    test_login_origin();
    test_login_body_rules();
    test_password_does_not_stay();
    test_random_failures();
    test_full_session_table();
    return session_test_finish("web_session_login");
}
