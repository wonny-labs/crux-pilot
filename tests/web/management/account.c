/* The account routes of the management API, which Issue #186 implemented: the current user and the
 * change of one's own password. Their own suites hold the detail. This one shows the four cases
 * that Issue #187 asks of every route, through the same image as the status and configuration
 * routes, so that the access rules of the three families are held to one account table. */
#include "management_test.h"

#define ME "/api/v1/session/me"
#define PASSWORD_URL "/api/v1/session/me/password"

static void test_current_user(void)
{
    mgmt_setup();
    mgmt_expect_body(
        mgmt_get(mgmt_jar("admin"), ME), 200,
        "{\"user\":\"admin\",\"role\":\"administrator\",\"must_change_password\":false}");
    mgmt_expect_body(
        mgmt_get(mgmt_jar("stranger"), ME), 200,
        "{\"user\":\"stranger\",\"role\":\"stranger\",\"must_change_password\":false}");
    /* A session needs no permission, so a forced change can read it, and it says why. */
    mgmt_expect_body(mgmt_get(mgmt_jar("forced"), ME), 200,
                     "{\"user\":\"forced\",\"role\":\"both\",\"must_change_password\":true}");
}

static void test_current_user_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("admin");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_GET, ME, NULL), 401, "unauthorized");
}

static void test_current_user_invalid_requests(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    mgmt_expect_plain(mgmt_post(jar, ME, NULL), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_put(jar, ME, "{}"), 405, "Method Not Allowed");
    mgmt_expect_error(mgmt_get(jar, ME "/x"), 404, "not_found");
}

/* Queues a change, runs the worker step, and returns the poll's reply. */
static const fake_reply_t *change_password(const session_jar_t *jar, const char *current,
                                           const char *replacement)
{
    char body[512];
    char id[80];
    const fake_reply_t *reply;

    snprintf(body, sizeof(body), "{\"current_password\":\"%s\",\"new_password\":\"%s\"}", current,
             replacement);
    reply = mgmt_post(jar, PASSWORD_URL, body);
    CHECK(reply->status == 202);
    CHECK(session_json_string(reply->body, "id", id, sizeof(id)));
    while (web_session_work_once()) {
    }
    return session_poll(id);
}

static void test_password_change(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("stranger");
    mgmt_expect_body(change_password(jar, MGMT_PASSWORD, "a different long password"), 200,
                     "{\"state\":\"success\"}");
    /* The new password logs in, and the old one no longer does. */
    CHECK(session_login("stranger", "a different long password", NULL)->status == 200);
    mgmt_expect_body(session_login("stranger", MGMT_PASSWORD, NULL), 200,
                     "{\"state\":\"rejected\"}");
}

static void test_password_change_with_a_forced_change(void)
{
    mgmt_setup();
    /* The route is session-only, and the account service authorises it, so a forced session may. */
    mgmt_expect_body(
        change_password(mgmt_jar("forced"), MGMT_PASSWORD, "a different long password"), 200,
        "{\"state\":\"success\"}");
}

static void test_password_change_rejections(void)
{
    mgmt_setup();
    mgmt_expect_body(
        change_password(mgmt_jar("both"), "not the current one", "a different long password"), 200,
        "{\"state\":\"rejected\",\"code\":\"invalid_field\",\"fields\":[{"
        "\"name\":\"current_password\",\"code\":\"incorrect\"}]}");
    mgmt_expect_body(change_password(mgmt_jar("reader"), MGMT_PASSWORD, "short"), 200,
                     "{\"state\":\"rejected\",\"code\":\"invalid_field\",\"fields\":[{"
                     "\"name\":\"new_password\",\"code\":\"rule_not_met\"}]}");
}

static void test_password_change_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("admin");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_POST, PASSWORD_URL,
                                     "{\"current_password\":\"a\",\"new_password\":\"b\"}"),
                      401, "unauthorized");
}

static void test_password_change_forgery_checks(void)
{
    const session_jar_t *jar;
    mgmt_variation_t no_token = {.no_token = true};
    mgmt_variation_t no_origin = {.no_origin = true};
    const char *body = "{\"current_password\":\"a\",\"new_password\":\"b\"}";

    mgmt_setup();
    jar = mgmt_jar("admin");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_POST, PASSWORD_URL, body, no_token), 403,
                      "csrf_rejected");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_POST, PASSWORD_URL, body, no_origin), 403,
                      "origin_rejected");
}

static void test_password_change_invalid_input(void)
{
    static const char *const bodies[] = {
        "",
        "{",
        "{}",
        "[]",
        "{\"current_password\":\"a\"}",
        "{\"new_password\":\"a\"}",
        "{\"current_password\":1,\"new_password\":\"b\"}",
        "{\"current_password\":\"a\",\"new_password\":\"b\",\"extra\":\"c\"}",
        "{\"current_password\":\"a\",\"current_password\":\"a\",\"new_password\":\"b\"}",
    };
    static char big[CRUX_WEB_BODY_BUFFER + 16u];
    const session_jar_t *jar;
    mgmt_variation_t text = {.content_type = "text/plain"};

    mgmt_setup();
    jar = mgmt_jar("admin");
    for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        const fake_reply_t *reply = mgmt_post(jar, PASSWORD_URL, bodies[index]);

        mgmt_expect_error(reply, 400, "bad_request");
        CHECK(reply->closed);
    }
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_POST, PASSWORD_URL,
                                "{\"current_password\":\"a\",\"new_password\":\"b\"}", text),
                      400, "bad_request");
    memset(big, 'a', sizeof(big) - 1u);
    mgmt_expect_error(mgmt_post(jar, PASSWORD_URL, big), 413, "payload_too_large");
    mgmt_expect_plain(mgmt_get(jar, PASSWORD_URL), 405, "Method Not Allowed");
}

int main(void)
{
    mgmt_run_case("me: success", test_current_user);
    mgmt_run_case("me: unauthenticated", test_current_user_unauthenticated);
    mgmt_run_case("me: invalid requests", test_current_user_invalid_requests);
    mgmt_run_case("password: success", test_password_change);
    mgmt_run_case("password: a forced change", test_password_change_with_a_forced_change);
    mgmt_run_case("password: rejections", test_password_change_rejections);
    mgmt_run_case("password: unauthenticated", test_password_change_unauthenticated);
    mgmt_run_case("password: forgery checks", test_password_change_forgery_checks);
    mgmt_run_case("password: invalid input", test_password_change_invalid_input);
    return mgmt_finish("web_management_account");
}
