/* GET and POST /api/v1/system/reboot: success, no session, no permission, the forgery checks, a
 * refusal, a refusal at the second check, a failed reset, a body, other methods, a request while
 * one is pending, and what the answers carry. On the real core, session module, Access and service.
 */
#include "reboot_web.h"

#include <crux/web.h>

#define PENDING "{\"state\":\"pending\"}"
#define NONE "{\"state\":\"none\"}"
#define FAILED "{\"state\":\"failed\"}"

/* The steps of the service, which leave out the answers that the wrapper notes. */
static size_t service_steps(void)
{
    return reboot_trace_count() - reboot_trace_count_of(STEP_RESPOND);
}

/* The generated table, which the core reads. */
extern const crux_web_route_t *const crux_web_route_table[];
extern const size_t crux_web_route_count;

static void a_get_before_any_request(void)
{
    static const char *const users[] = {"admin", "rebooter"};

    web_reboot_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        const fake_reply_t *reply = web_reboot_get(web_reboot_jar(users[index]));

        web_reboot_expect_body(reply, 200, NONE);
        web_reboot_expect_headers(reply, "application/json");
        CHECK(!reply->closed);
    }
    CHECK(service_steps() == 0u);
}

static void a_get_needs_the_permission(void)
{
    static const char *const users[] = {"reader", "stranger", "forced"};

    web_reboot_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        web_reboot_expect_error(web_reboot_get(web_reboot_jar(users[index])), 403,
                                "permission_denied");
    }
    /* The core recorded each denial, and the service has nothing to record. */
    CHECK(reboot_denials() == 3u);
    CHECK(service_steps() == 0u);
}

static void a_get_without_a_session(void)
{
    web_reboot_setup();
    (void)web_reboot_jar("admin");
    web_reboot_expect_error(web_reboot_anonymous(CRUX_WEB_GET), 401, "unauthorized");
}

static void a_post_accepts_and_the_order_is_kept(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;
    size_t index = 0;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    reply = web_reboot_post(jar);
    web_reboot_expect_body(reply, 202, PENDING);
    web_reboot_expect_headers(reply, "application/json");
    CHECK(reply->closed);
    /* The policy, then the event, then the answer, and nothing after it until the worker runs. */
    CHECK(reboot_trace_count() == 3u);
    CHECK(reboot_trace_get(index)->kind == STEP_POLICY);
    ++index;
    CHECK(reboot_trace_get(index)->kind == STEP_EVENT &&
          reboot_trace_get(index)->value == CRUX_REBOOT_EVENT_REQUESTED);
    ++index;
    CHECK(reboot_trace_get(index)->kind == STEP_RESPOND && reboot_trace_get(index)->value == 202);
    CHECK(reboot_sleeps() == 0u && reboot_reset_calls() == 0u);
    web_reboot_expect_body(web_reboot_get(jar), 200, PENDING);
    /* The worker then runs the rest, in order: the delay, the wait, the second check, the reset. */
    reboot_trace_clear();
    CHECK(crux_reboot_work_once());
    CHECK(reboot_trace_get(0u)->kind == STEP_SLEEP && reboot_trace_get(0u)->value == 1000);
    CHECK(reboot_trace_get(reboot_trace_count() - 3u)->kind == STEP_POLICY);
    CHECK(reboot_trace_get(reboot_trace_count() - 2u)->kind == STEP_RESET);
    CHECK(reboot_trace_get(reboot_trace_count() - 1u)->kind == STEP_EVENT);
}

static void a_post_that_the_policy_refuses(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_reboot_setup();
    reboot_policy_always(false);
    reboot_policy_reason("Machine cycle running", 22u);
    jar = web_reboot_jar("admin");
    reply = web_reboot_post(jar);
    web_reboot_expect_body(reply, 423,
                           "{\"code\":\"reboot_refused\",\"reason\":\"Machine cycle running\"}");
    web_reboot_expect_headers(reply, "application/json");
    CHECK(reply->closed);
    /* The refusal is recorded before it is answered, and nothing is queued. */
    CHECK(reboot_trace_count() == 3u);
    CHECK(reboot_trace_get(1u)->kind == STEP_EVENT &&
          reboot_trace_get(1u)->value == CRUX_REBOOT_EVENT_REFUSED);
    CHECK(reboot_trace_get(2u)->kind == STEP_RESPOND && reboot_trace_get(2u)->value == 423);
    CHECK(!crux_reboot_work_once());
    /* GET shows it, with the reason. */
    web_reboot_expect_body(web_reboot_get(jar), 200,
                           "{\"state\":\"refused\",\"reason\":\"Machine cycle running\"}");
}

static void a_reason_is_escaped(void)
{
    const session_jar_t *jar;

    web_reboot_setup();
    reboot_policy_always(false);
    reboot_policy_reason("Say \"no\" to C:\\run", 19u);
    jar = web_reboot_jar("admin");
    web_reboot_expect_body(
        web_reboot_post(jar), 423,
        "{\"code\":\"reboot_refused\",\"reason\":\"Say \\\"no\\\" to C:\\\\run\"}");
    web_reboot_expect_body(web_reboot_get(jar), 200,
                           "{\"state\":\"refused\",\"reason\":\"Say \\\"no\\\" to C:\\\\run\"}");
    /* A reason that is not one that a client may see is the default, still valid JSON. */
    reboot_policy_reason("bad\x01text", 8u);
    web_reboot_expect_body(web_reboot_post(jar), 423,
                           "{\"code\":\"reboot_refused\",\"reason\":\"" CRUX_REBOOT_DEFAULT_REASON
                           "\"}");
    /* The longest one, 63 bytes. */
    char longest[64];
    char expected[160];

    memset(longest, 'x', 63);
    longest[63] = '\0';
    reboot_policy_reason(longest, 64u);
    snprintf(expected, sizeof(expected), "{\"code\":\"reboot_refused\",\"reason\":\"%s\"}",
             longest);
    web_reboot_expect_body(web_reboot_post(jar), 423, expected);
    /* And one made of 63 escapes cannot overflow the answer. */
    memset(longest, '"', 63);
    reboot_policy_reason(longest, 64u);
    char escaped[200];
    int used = snprintf(escaped, sizeof(escaped), "{\"code\":\"reboot_refused\",\"reason\":\"");

    for (size_t index = 0; index < 63u; ++index) {
        escaped[used++] = '\\';
        escaped[used++] = '"';
    }
    memcpy(escaped + used, "\"}", 3u);
    web_reboot_expect_body(web_reboot_post(jar), 423, escaped);
}

static void a_refusal_at_the_second_check_shows_on_get(void)
{
    const bool script[] = {true, false};
    const session_jar_t *jar;

    web_reboot_setup();
    reboot_policy_script(script, 2u);
    reboot_policy_reason("Machine cycle started", 22u);
    jar = web_reboot_jar("rebooter");
    web_reboot_expect_body(web_reboot_post(jar), 202, PENDING);
    CHECK(crux_reboot_work_once());
    web_reboot_expect_body(web_reboot_get(jar), 200,
                           "{\"state\":\"refused\",\"reason\":\"Machine cycle started\"}");
    CHECK(reboot_reset_calls() == 0u);
}

static void a_failed_reset_shows_on_get(void)
{
    const session_jar_t *jar;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    web_reboot_expect_body(web_reboot_post(jar), 202, PENDING);
    CHECK(crux_reboot_work_once());
    /* The failure carries no text: the operator's detail stays on the device. */
    web_reboot_expect_body(web_reboot_get(jar), 200, FAILED);
    /* A new request replaces it. */
    web_reboot_expect_body(web_reboot_post(jar), 202, PENDING);
    web_reboot_expect_body(web_reboot_get(jar), 200, PENDING);
}

static void a_second_post_while_pending_starts_nothing(void)
{
    const session_jar_t *jar;
    size_t policy_calls;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    (void)web_reboot_jar("admin");
    web_reboot_expect_body(web_reboot_post(jar), 202, PENDING);
    policy_calls = reboot_policy_calls();
    /* Even for a session that the policy would now refuse. */
    reboot_policy_always(false);
    web_reboot_expect_body(web_reboot_post(web_reboot_jar("admin")), 202, PENDING);
    web_reboot_expect_body(web_reboot_post(jar), 202, PENDING);
    CHECK(policy_calls == 1u && reboot_policy_calls() == 0u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 3u);
    web_reboot_expect_body(web_reboot_get(jar), 200, PENDING);
    reboot_policy_always(true);
    CHECK(crux_reboot_work_once());
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

static void a_post_needs_the_permission(void)
{
    static const char *const users[] = {"reader", "stranger", "forced"};

    web_reboot_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        web_reboot_expect_error(web_reboot_post(web_reboot_jar(users[index])), 403,
                                "permission_denied");
    }
    CHECK(reboot_denials() == 3u);
    /* The policy was not asked, and nothing was recorded or queued. */
    CHECK(service_steps() == 0u);
    CHECK(!crux_reboot_work_once());
}

static void a_post_without_a_session(void)
{
    web_reboot_setup();
    (void)web_reboot_jar("admin");
    web_reboot_expect_error(web_reboot_anonymous(CRUX_WEB_POST), 401, "unauthorized");
    CHECK(service_steps() == 0u);
}

static void the_forgery_checks_come_with_the_session(void)
{
    const session_jar_t *jar;
    web_reboot_variation_t no_token = {.no_token = true};
    web_reboot_variation_t no_origin = {.no_origin = true};

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    web_reboot_expect_error(web_reboot_vary(jar, CRUX_WEB_POST, no_token), 403, "csrf_rejected");
    web_reboot_expect_error(web_reboot_vary(jar, CRUX_WEB_POST, no_origin), 403, "origin_rejected");
    CHECK(service_steps() == 0u && !crux_reboot_work_once());
    web_reboot_expect_body(web_reboot_get(jar), 200, NONE);
}

static void a_body_is_refused_before_anything_runs(void)
{
    static const char *const bodies[] = {" ", "{}", "{\"state\":\"pending\"}", "0", "\n"};
    const session_jar_t *jar;
    char big[400];

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        web_reboot_variation_t with_body = {.body = bodies[index]};

        web_reboot_expect_error(web_reboot_vary(jar, CRUX_WEB_POST, with_body), 413,
                                "payload_too_large");
    }
    memset(big, 'x', sizeof(big) - 1u);
    big[sizeof(big) - 1u] = '\0';
    web_reboot_variation_t with_big = {.body = big};
    web_reboot_expect_error(web_reboot_vary(jar, CRUX_WEB_POST, with_big), 413,
                            "payload_too_large");
    /* The core refuses before any session lookup, so an anonymous one gets the same. */
    session_request_t anonymous = {.method = CRUX_WEB_POST,
                                   .url = REBOOT_URL,
                                   .host = SESSION_TEST_HOST,
                                   .origin = SESSION_TEST_ORIGIN,
                                   .content_type = "application/json",
                                   .body = "{}"};
    web_reboot_expect_error(session_send(&anonymous), 413, "payload_too_large");
    CHECK(service_steps() == 0u && reboot_policy_calls() == 0u);
    web_reboot_expect_body(web_reboot_get(jar), 200, NONE);
}

static void other_methods_and_paths(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_reboot_setup();
    jar = web_reboot_jar("admin");
    /* The path has GET and POST. The server answers any other method itself. */
    for (unsigned method = CRUX_WEB_PUT; method <= CRUX_WEB_PATCH; method <<= 1) {
        if (method == CRUX_WEB_DELETE || method == CRUX_WEB_PUT || method == CRUX_WEB_PATCH) {
            reply = session_with(jar, method, REBOOT_URL, NULL);
            CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 405);
        }
    }
    /* A path under it is no route. A query is ignored. */
    reply = session_with(jar, CRUX_WEB_GET, REBOOT_URL "/now", NULL);
    CHECK(reply->status == 404);
    reply = session_with(jar, CRUX_WEB_POST, REBOOT_URL "/now", NULL);
    CHECK(reply->status == 404);
    web_reboot_expect_body(session_with(jar, CRUX_WEB_GET, REBOOT_URL "?x=1", NULL), 200, NONE);
    CHECK(service_steps() == 0u);
}

static void a_service_that_is_not_started(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_reboot_setup_not_started();
    jar = web_reboot_jar("admin");
    web_reboot_expect_error(web_reboot_get(jar), 503, "not_ready");
    reply = web_reboot_post(jar);
    web_reboot_expect_error(reply, 503, "not_ready");
    web_reboot_expect_retry_after(reply);
    CHECK(reply->closed);
    CHECK(service_steps() == 0u);
}

static void a_lock_that_does_not_come_in_time_is_busy(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_reboot_setup();
    jar = web_reboot_jar("rebooter");
    web_reboot_fail_lock(20u);
    reply = web_reboot_get(jar);
    web_reboot_expect_error(reply, 503, "busy");
    web_reboot_expect_retry_after(reply);
    reply = web_reboot_post(jar);
    web_reboot_expect_error(reply, 503, "busy");
    web_reboot_expect_retry_after(reply);
    CHECK(reply->closed);
    web_reboot_fail_lock(0u);
    /* Nothing was asked, recorded or queued. */
    CHECK(service_steps() == 0u && reboot_policy_calls() == 0u);
    web_reboot_expect_body(web_reboot_get(jar), 200, NONE);
    web_reboot_expect_body(web_reboot_post(jar), 202, PENDING);
}

static void the_routes_are_in_the_table_with_the_permission(void)
{
    bool get = false, post = false;

    for (size_t index = 0; index < crux_web_route_count; ++index) {
        const crux_web_route_t *route = crux_web_route_table[index];

        if (strcmp(route->path, REBOOT_URL) != 0) {
            continue;
        }
        /* The access class is the permission, which the core enforces before the handler. */
        CHECK(strcmp(route->access, CRUX_REBOOT_PERMISSION) == 0);
        if (route->methods == CRUX_WEB_GET) {
            get = true;
        } else if (route->methods == CRUX_WEB_POST) {
            post = true;
            CHECK(route->body_max == 0u);
        } else {
            CHECK(false);
        }
    }
    CHECK(get && post);
}

int main(void)
{
    reboot_run_case("a get before any request", a_get_before_any_request);
    reboot_run_case("a get needs the permission", a_get_needs_the_permission);
    reboot_run_case("a get without a session", a_get_without_a_session);
    reboot_run_case("a post accepts and the order is kept", a_post_accepts_and_the_order_is_kept);
    reboot_run_case("a post that the policy refuses", a_post_that_the_policy_refuses);
    reboot_run_case("a reason is escaped", a_reason_is_escaped);
    reboot_run_case("a refusal at the second check shows on get",
                    a_refusal_at_the_second_check_shows_on_get);
    reboot_run_case("a failed reset shows on get", a_failed_reset_shows_on_get);
    reboot_run_case("a second post while pending starts nothing",
                    a_second_post_while_pending_starts_nothing);
    reboot_run_case("a post needs the permission", a_post_needs_the_permission);
    reboot_run_case("a post without a session", a_post_without_a_session);
    reboot_run_case("the forgery checks come with the session",
                    the_forgery_checks_come_with_the_session);
    reboot_run_case("a body is refused before anything runs",
                    a_body_is_refused_before_anything_runs);
    reboot_run_case("other methods and paths", other_methods_and_paths);
    reboot_run_case("a service that is not started", a_service_that_is_not_started);
    reboot_run_case("a lock that does not come in time is busy",
                    a_lock_that_does_not_come_in_time_is_busy);
    reboot_run_case("the routes are in the table with the permission",
                    the_routes_are_in_the_table_with_the_permission);
    return reboot_finish("web_reboot_routes");
}
