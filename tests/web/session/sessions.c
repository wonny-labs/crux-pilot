/* The session lookup: missing, forged and expired sessions, the forgery rules, the idle and absolute
 * limits, sessions ended by account changes, logout, the current user and the response headers. */
#include "session_test.h"

extern crux_access_session_t session_test_route_session;
extern unsigned session_test_route_calls;

static void expect_error(const fake_reply_t *reply, int status, const char *code)
{
    char expected[64];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == status);
    if (strcmp(reply->body, expected) != 0) {
        printf("  expected %s, got %d %s\n", expected, reply->status, reply->body);
    }
    CHECK(strcmp(reply->body, expected) == 0);
}

static const fake_reply_t *get_with_cookie(const char *cookie, const char *url)
{
    session_request_t request = {.method = CRUX_WEB_GET,
                                 .url = url,
                                 .cookie = cookie,
                                 .host = SESSION_TEST_HOST};

    return session_send(&request);
}

static void test_missing_session(void)
{
    session_jar_t jar;

    session_test_setup();
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &jar)->status == 200);
    expect_error(get_with_cookie(NULL, "/api/v1/session/me"), 401, "unauthorized");
    expect_error(get_with_cookie("", "/api/v1/session/me"), 401, "unauthorized");
    expect_error(get_with_cookie("XSRF-TOKEN=abc; other=1", "/api/v1/session/me"), 401,
                 "unauthorized");
    /* The token alone, without its name, is not a session. */
    expect_error(get_with_cookie(jar.session, "/api/v1/session/me"), 401, "unauthorized");
    CHECK(session_test_route_calls == 0u);
}

static void test_forged_sessions(void)
{
    session_jar_t jar;
    char cookie[200];
    char token[64];

    session_test_setup();
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &jar)->status == 200);
    /* A well-formed token that was never issued. */
    expect_error(get_with_cookie("crux_session=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                                 "/api/v1/session/me"),
                 401, "unauthorized");
    /* The issued CSRF token in the session cookie. */
    snprintf(cookie, sizeof(cookie), "crux_session=%s", jar.xsrf);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    /* A captured prefix, as a NUL in the value leaves, and one character more. */
    snprintf(token, sizeof(token), "%s", jar.session);
    token[20] = '\0';
    snprintf(cookie, sizeof(cookie), "crux_session=%s", token);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    snprintf(cookie, sizeof(cookie), "crux_session=%sA", jar.session);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    /* One changed character. */
    snprintf(token, sizeof(token), "%s", jar.session);
    token[10] = token[10] == 'A' ? 'B' : 'A';
    snprintf(cookie, sizeof(cookie), "crux_session=%s", token);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    /* Quoted, percent-encoded, a near name, and the name twice. */
    snprintf(cookie, sizeof(cookie), "crux_session=\"%s\"", jar.session);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    snprintf(cookie, sizeof(cookie), "crux_session=%%41%s", jar.session + 3);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    snprintf(cookie, sizeof(cookie), "xcrux_session=%s", jar.session);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    snprintf(cookie, sizeof(cookie), "Crux_session=%s", jar.session);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    snprintf(cookie, sizeof(cookie), "crux_session=%s; crux_session=%s", jar.session,
             jar.session);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    snprintf(cookie, sizeof(cookie),
             "crux_session=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA; crux_session=%s",
             jar.session);
    expect_error(get_with_cookie(cookie, "/api/v1/session/me"), 401, "unauthorized");
    /* The real one, among other cookies and with spacing, still works. */
    snprintf(cookie, sizeof(cookie), "a=1;crux_session=%s ;  b=2", jar.session);
    CHECK(get_with_cookie(cookie, "/api/v1/session/me")->status == 200);
}

static void test_session_routes_need_no_token_on_get(void)
{
    session_jar_t jar;

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(get_with_cookie(jar.header, "/api/v1/test/session")->status == 200);
    CHECK(session_test_route_calls == 1u);
}

/* A state-changing request needs the CSRF token, compared whole, and an accepted Origin. */
static void test_forgery_rules(void)
{
    static const unsigned methods[] = {CRUX_WEB_POST, CRUX_WEB_PUT, CRUX_WEB_DELETE,
                                       CRUX_WEB_PATCH};
    session_jar_t jar;
    session_jar_t other;
    char token[64];
    session_request_t request;

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(session_login("forced", SESSION_TEST_PASSWORD, &other)->status == 200);
    for (size_t index = 0; index < sizeof(methods) / sizeof(methods[0]); ++index) {
        request = (session_request_t){.method = methods[index],
                                      .url = "/api/v1/test/session",
                                      .cookie = jar.header,
                                      .origin = SESSION_TEST_ORIGIN,
                                      .host = SESSION_TEST_HOST,
                                      .xsrf = jar.xsrf};
        CHECK(session_send(&request)->status == 200);

        request.xsrf = NULL;
        expect_error(session_send(&request), 403, "csrf_rejected");
        request.xsrf = "";
        expect_error(session_send(&request), 403, "csrf_rejected");
        request.xsrf = other.xsrf;
        expect_error(session_send(&request), 403, "csrf_rejected");
        request.xsrf = jar.session;
        expect_error(session_send(&request), 403, "csrf_rejected");
        snprintf(token, sizeof(token), "%s", jar.xsrf);
        token[42] = '\0';
        request.xsrf = token;
        expect_error(session_send(&request), 403, "csrf_rejected");
        snprintf(token, sizeof(token), "%s=", jar.xsrf);
        expect_error(session_send(&request), 403, "csrf_rejected");

        request.xsrf = jar.xsrf;
        request.origin = NULL;
        expect_error(session_send(&request), 403, "origin_rejected");
        request.origin = "https://evil.test";
        expect_error(session_send(&request), 403, "origin_rejected");
        request.origin = "https://device.test.evil.test";
        expect_error(session_send(&request), 403, "origin_rejected");
        request.origin = "null";
        expect_error(session_send(&request), 403, "origin_rejected");
    }
    /* Every refused request ran no handler: the four accepted ones did. */
    CHECK(session_test_route_calls == 4u);
    /* A forged request with no session is 401, which says nothing about a token. */
    request = (session_request_t){.method = CRUX_WEB_POST,
                                  .url = "/api/v1/test/session",
                                  .origin = SESSION_TEST_ORIGIN,
                                  .host = SESSION_TEST_HOST,
                                  .xsrf = jar.xsrf};
    expect_error(session_send(&request), 401, "unauthorized");
    /* Logout follows the same rules, and a refused logout ends nothing. */
    request = (session_request_t){.method = CRUX_WEB_DELETE,
                                  .url = "/api/v1/session",
                                  .cookie = jar.header,
                                  .origin = SESSION_TEST_ORIGIN,
                                  .host = SESSION_TEST_HOST};
    expect_error(session_send(&request), 403, "csrf_rejected");
    CHECK(get_with_cookie(jar.header, "/api/v1/session/me")->status == 200);
}

/* An explicit allow list replaces the Host rule. */
static void test_configured_origins(void)
{
    static const char *const origins[] = {"https://controller.example", "https://192.0.2.10"};
    static const crux_web_session_config_t config = {"https", origins, 2u};
    session_jar_t jar;

    session_test_setup();
    CHECK(crux_web_session_init(&config) == CRUX_WEB_OK);
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 403);
    {
        session_request_t login = {.method = CRUX_WEB_POST,
                                   .url = "/api/v1/session",
                                   .origin = "https://192.0.2.10",
                                   .host = "anything",
                                   .content_type = "application/json",
                                   .body = "{\"user\":\"viewer\",\"password\":\"" SESSION_TEST_PASSWORD
                                           "\"}"};
        const fake_reply_t *reply = session_send(&login);
        char id[80];

        CHECK(reply->status == 202);
        CHECK(session_json_string(reply->body, "id", id, sizeof(id)));
        CHECK(web_session_work_once());
        CHECK(session_poll(id)->status == 200);
    }
    {
        const crux_web_session_config_t bad = {"ftp", NULL, 0u};
        const crux_web_session_config_t missing = {"https", NULL, 1u};

        CHECK(crux_web_session_init(&bad) == CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_session_init(&missing) == CRUX_WEB_ERR_INVALID_ARGUMENT);
        CHECK(crux_web_session_init(NULL) == CRUX_WEB_ERR_INVALID_ARGUMENT);
    }
}

static void test_wrong_role(void)
{
    session_jar_t viewer;
    session_jar_t admin;

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &viewer)->status == 200);
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &admin)->status == 200);
    expect_error(session_with(&viewer, CRUX_WEB_GET, "/api/v1/test/admin", NULL), 403,
                 "permission_denied");
    expect_error(session_with(&viewer, CRUX_WEB_POST, "/api/v1/test/admin", NULL), 403,
                 "permission_denied");
    CHECK(session_test_route_calls == 0u);
    CHECK(session_with(&admin, CRUX_WEB_GET, "/api/v1/test/admin", NULL)->status == 200);
    CHECK(session_with(&admin, CRUX_WEB_POST, "/api/v1/test/admin", NULL)->status == 200);
    /* A forced change may reach session routes and its own user, and no permission route. */
    {
        session_jar_t forced;

        CHECK(session_login("forced", SESSION_TEST_PASSWORD, &forced)->status == 200);
        CHECK(strcmp(session_with(&forced, CRUX_WEB_GET, "/api/v1/session/me", NULL)->body,
                     "{\"user\":\"forced\",\"role\":\"viewer\",\"must_change_password\":true}") ==
              0);
    }
}

/* GET does not refresh the idle timer, a state-changing request does, and the absolute limit holds
 * whatever the activity. */
static void test_idle_and_absolute_expiry(void)
{
    session_jar_t jar;

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    session_clock_advance(800000u);
    CHECK(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);
    session_clock_advance(200000u);
    expect_error(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL), 401,
                 "unauthorized");
    CHECK(web_session_table_count() == 0u);

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    session_clock_advance(800000u);
    CHECK(session_with(&jar, CRUX_WEB_POST, "/api/v1/test/session", NULL)->status == 200);
    session_clock_advance(800000u);
    CHECK(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    for (unsigned step = 0; step < 35u; ++step) {
        session_clock_advance(800000u);
        CHECK(session_with(&jar, CRUX_WEB_POST, "/api/v1/test/session", NULL)->status == 200);
    }
    session_clock_advance(801000u);
    expect_error(session_with(&jar, CRUX_WEB_POST, "/api/v1/test/session", NULL), 401,
                 "unauthorized");
}

/* A role change and an administrator's password reset end the account's web sessions. */
static void test_account_changes_end_sessions(void)
{
    session_jar_t jar;
    session_jar_t admin;

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(session_login("admin", SESSION_TEST_PASSWORD, &admin)->status == 200);
    CHECK(crux_access_account_set_role(session_admin, "viewer", "administrator") == CRUX_ACCESS_OK);
    expect_error(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL), 401,
                 "unauthorized");
    CHECK(session_with(&admin, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(crux_access_account_reset_password(session_admin, "viewer", "a new long password",
                                             strlen("a new long password")) == CRUX_ACCESS_OK);
    expect_error(session_with(&jar, CRUX_WEB_POST, "/api/v1/test/session", NULL), 401,
                 "unauthorized");

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(crux_access_account_disable(session_admin, "viewer") == CRUX_ACCESS_OK);
    expect_error(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL), 401,
                 "unauthorized");
}

static const fake_reply_t *change_password(const session_jar_t *jar, const char *current,
                                           const char *next, char *id, size_t size)
{
    char body[400];
    const fake_reply_t *reply;

    snprintf(body, sizeof(body), "{\"current_password\":\"%s\",\"new_password\":\"%s\"}", current,
             next);
    reply = session_with(jar, CRUX_WEB_POST, "/api/v1/session/me/password", body);
    id[0] = '\0';
    if (reply->status == 202) {
        CHECK(session_json_string(reply->body, "id", id, size));
        CHECK(reply->closed);
    }
    return reply;
}

/* One's own password change keeps this session and ends the account's others. */
static void test_own_password_change(void)
{
    session_jar_t jar;
    session_jar_t second;
    char id[80];
    const fake_reply_t *reply;
    const char *cookies[2];

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &second)->status == 200);

    CHECK(change_password(&jar, "wrong current pw", "a brand new password", id, sizeof(id))
              ->status == 202);
    CHECK(web_session_work_once());
    reply = session_poll(id);
    CHECK(strcmp(reply->body, "{\"state\":\"rejected\",\"code\":\"invalid_field\",\"fields\":[{"
                              "\"name\":\"current_password\",\"code\":\"incorrect\"}]}") == 0);

    CHECK(change_password(&jar, SESSION_TEST_PASSWORD, "short", id, sizeof(id))->status == 202);
    CHECK(web_session_work_once());
    reply = session_poll(id);
    CHECK(strcmp(reply->body, "{\"state\":\"rejected\",\"code\":\"invalid_field\",\"fields\":[{"
                              "\"name\":\"new_password\",\"code\":\"rule_not_met\"}]}") == 0);

    CHECK(change_password(&jar, SESSION_TEST_PASSWORD, "a brand new password", id, sizeof(id))
              ->status == 202);
    CHECK(web_session_work_once());
    reply = session_poll(id);
    CHECK(reply->status == 200 && strcmp(reply->body, "{\"state\":\"success\"}") == 0);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 2u) == 0u);
    CHECK(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);
    expect_error(session_with(&second, CRUX_WEB_GET, "/api/v1/session/me", NULL), 401,
                 "unauthorized");
    CHECK(session_login("viewer", "a brand new password", NULL)->status == 200);

    /* The change needs the token and Origin like any other. */
    {
        session_request_t request = {.method = CRUX_WEB_POST,
                                     .url = "/api/v1/session/me/password",
                                     .cookie = jar.header,
                                     .origin = SESSION_TEST_ORIGIN,
                                     .host = SESSION_TEST_HOST,
                                     .content_type = "application/json",
                                     .body = "{\"current_password\":\"x\",\"new_password\":\"y\"}"};
        expect_error(session_send(&request), 403, "csrf_rejected");
    }
    /* A session that ends while its change waits is told so. */
    CHECK(change_password(&jar, "a brand new password", "another new password", id, sizeof(id))
              ->status == 202);
    CHECK(session_with(&jar, CRUX_WEB_DELETE, "/api/v1/session", NULL)->status == 200);
    CHECK(web_session_work_once());
    reply = session_poll(id);
    CHECK(strcmp(reply->body, "{\"state\":\"rejected\",\"code\":\"unauthorized\"}") == 0);
}

static void test_forced_change_flow(void)
{
    session_jar_t jar;
    char id[80];

    session_test_setup();
    CHECK(session_login("forced", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(change_password(&jar, SESSION_TEST_PASSWORD, "the chosen password", id, sizeof(id))
              ->status == 202);
    CHECK(web_session_work_once());
    CHECK(strcmp(session_poll(id)->body, "{\"state\":\"success\"}") == 0);
    CHECK(strcmp(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->body,
                 "{\"user\":\"forced\",\"role\":\"viewer\",\"must_change_password\":false}") == 0);
}

static void test_logout(void)
{
    session_jar_t jar;
    const fake_reply_t *reply;
    const char *cookies[4];

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    CHECK(web_session_table_count() == 1u);
    reply = session_with(&jar, CRUX_WEB_DELETE, "/api/v1/session", NULL);
    CHECK(reply->status == 200 && reply->body_length == 0u);
    CHECK(session_reply_headers(reply, "Set-Cookie", cookies, 4u) == 2u);
    CHECK(strcmp(cookies[0], "crux_session=; Secure; HttpOnly; SameSite=Strict; Path=/; Max-Age=0") ==
          0);
    CHECK(strcmp(cookies[1], "XSRF-TOKEN=; Secure; SameSite=Strict; Path=/; Max-Age=0") == 0);
    CHECK(web_session_table_count() == 0u);
    expect_error(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL), 401,
                 "unauthorized");
    expect_error(session_with(&jar, CRUX_WEB_DELETE, "/api/v1/session", NULL), 401,
                 "unauthorized");
}

/* Every reply from these routes carries the security headers, and none carries HSTS or CORS. */
static void test_response_headers(void)
{
    session_jar_t jar;
    char id[80];
    const fake_reply_t *replies[6];
    static fake_reply_t copies[6];
    size_t count = 0;

    session_test_setup();
    copies[count++] = *session_post_login("viewer", SESSION_TEST_PASSWORD, id, sizeof(id));
    copies[count++] = *session_poll(id);
    CHECK(web_session_work_once());
    copies[count++] = *session_poll(id);
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    copies[count++] = *session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL);
    copies[count++] = *get_with_cookie(NULL, "/api/v1/session/me");
    copies[count++] = *session_with(&jar, CRUX_WEB_DELETE, "/api/v1/session", NULL);
    for (size_t index = 0; index < count; ++index) {
        const char *value[2];

        replies[index] = &copies[index];
        CHECK(session_reply_headers(replies[index], "Content-Security-Policy", value, 2u) == 1u &&
              strcmp(value[0], "default-src 'self'; frame-ancestors 'none'") == 0);
        CHECK(session_reply_headers(replies[index], "X-Content-Type-Options", value, 2u) == 1u &&
              strcmp(value[0], "nosniff") == 0);
        CHECK(session_reply_headers(replies[index], "Referrer-Policy", value, 2u) == 1u &&
              strcmp(value[0], "no-referrer") == 0);
        CHECK(session_reply_headers(replies[index], "Cache-Control", value, 2u) == 1u &&
              strcmp(value[0], "no-store") == 0);
        CHECK(session_reply_headers(replies[index], "Strict-Transport-Security", value, 2u) == 0u);
        CHECK(session_reply_headers(replies[index], "Access-Control-Allow-Origin", value, 2u) ==
              0u);
    }
}

/* A failing hash fails closed, with 503 and no session. */
static void test_hash_failure(void)
{
    session_jar_t jar;

    session_test_setup();
    CHECK(session_login("viewer", SESSION_TEST_PASSWORD, &jar)->status == 200);
    session_fake_hash_fail(true);
    expect_error(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL), 503, "not_ready");
    session_fake_hash_fail(false);
    CHECK(session_with(&jar, CRUX_WEB_GET, "/api/v1/session/me", NULL)->status == 200);
}

int main(void)
{
    test_missing_session();
    test_forged_sessions();
    test_session_routes_need_no_token_on_get();
    test_forgery_rules();
    test_configured_origins();
    test_wrong_role();
    test_idle_and_absolute_expiry();
    test_account_changes_end_sessions();
    test_own_password_change();
    test_forced_change_flow();
    test_logout();
    test_response_headers();
    test_hash_failure();
    return session_test_finish("web_session_sessions");
}
