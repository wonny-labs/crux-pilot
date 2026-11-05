/* GET /api/v1/status: success, no session, a session that may see little or nothing, invalid
 * requests, a failing source, and a service that is not ready or has too much to say. */
#include "management_test.h"

#define STATUS_URL "/api/v1/status"

static bool has(const fake_reply_t *reply, const char *text)
{
    return strstr(reply->body, text) != NULL;
}

static void test_success(void)
{
    const fake_reply_t *reply;

    mgmt_setup();
    reply = mgmt_get(mgmt_jar("admin"), STATUS_URL);
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == 200);
    mgmt_expect_headers(reply, "application/json");
    CHECK(strncmp(reply->body, "{\"items\":[", 10) == 0);
    CHECK(has(reply, "{\"name\":\"version.platform\",\"status\":\"ok\",\"type\":\"text\","
                     "\"value\":\"1.2.3\"}"));
    CHECK(has(reply, "{\"name\":\"version.project\",\"status\":\"ok\",\"type\":\"text\","
                     "\"value\":\"pilot-7\"}"));
    CHECK(has(reply, "{\"name\":\"system.uptime_ms\",\"status\":\"ok\",\"type\":\"u64\","
                     "\"value\":\"1000\"}"));
    CHECK(has(reply, "{\"name\":\"net.link\",\"status\":\"ok\",\"type\":\"bool\",\"value\":true}"));
    CHECK(has(reply, "{\"name\":\"app.cycle_count\",\"status\":\"ok\",\"type\":\"u32\","
                     "\"value\":42}"));
    CHECK(reply->body[reply->body_length - 1u] == '}');
    CHECK(!reply->closed);
}

static void test_each_session_sees_its_own_items(void)
{
    mgmt_setup();
    /* status.read gives the built-in items, and not a project item with its own permission. */
    const fake_reply_t *reader = mgmt_get(mgmt_jar("reader"), STATUS_URL);
    CHECK(reader->status == 200);
    CHECK(has(reader, "\"version.platform\""));
    CHECK(!has(reader, "app.cycle_count"));
    /* app.read gives the project item alone, which is why the route is session-only. */
    mgmt_expect_body(mgmt_get(mgmt_jar("app"), STATUS_URL), 200,
                     "{\"items\":[{\"name\":\"app.cycle_count\",\"status\":\"ok\",\"type\":\"u32\","
                     "\"value\":42}]}");
}

static void test_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("admin");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_GET, STATUS_URL, NULL), 401, "unauthorized");
    session_request_t forged = {.method = CRUX_WEB_GET,
                                .url = STATUS_URL,
                                .host = SESSION_TEST_HOST,
                                .cookie =
                                    "crux_session=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"};
    mgmt_expect_error(session_send(&forged), 401, "unauthorized");
}

static void test_a_session_without_a_permission_gets_an_empty_list_and_never_403(void)
{
    static const char *const users[] = {"stranger", "writer", "saver", "forced"};

    mgmt_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        mgmt_expect_body(mgmt_get(mgmt_jar(users[index]), STATUS_URL), 200, "{\"items\":[]}");
    }
}

static void test_a_failed_source_does_not_hide_the_rest(void)
{
    const fake_reply_t *reply;

    mgmt_setup();
    status_fake_system(CRUX_ERR_IO, CRUX_ERR_IO, CRUX_WATCHDOG_IDLE);
    status_fake_net(CRUX_ERR_NO_DATA, (crux_mgmt_status_net_t){0});
    reply = mgmt_get(mgmt_jar("admin"), STATUS_URL);
    CHECK(reply->status == 200);
    CHECK(has(reply, "{\"name\":\"system.watchdog_state\",\"status\":\"io\"}"));
    CHECK(has(reply, "{\"name\":\"net.ipv4\",\"status\":\"no_data\"}"));
    CHECK(has(reply, "\"name\":\"version.platform\""));
}

static void test_invalid_requests(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    /* The route has GET only, and the server answers the rest itself. */
    mgmt_expect_plain(mgmt_post(jar, STATUS_URL, NULL), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_put(jar, STATUS_URL, "{}"), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_delete(jar, STATUS_URL), 405, "Method Not Allowed");
    /* A path under it is the core's JSON 404, and a query is not read. */
    mgmt_expect_error(mgmt_get(jar, STATUS_URL "/x"), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, STATUS_URL "/"), 404, "not_found");
    CHECK(mgmt_get(jar, STATUS_URL "?verbose=1&x=%00")->status == 200);
    /* A body on a GET is refused by the core, and the connection closes. */
    session_request_t with_body = {.method = CRUX_WEB_GET,
                                   .url = STATUS_URL,
                                   .host = SESSION_TEST_HOST,
                                   .cookie = jar->header,
                                   .content_type = "application/json",
                                   .body = "{}"};
    const fake_reply_t *reply = session_send(&with_body);
    mgmt_expect_error(reply, 400, "bad_request");
    CHECK(reply->closed);
}

static void test_not_ready(void)
{
    const mgmt_options_t options = {.no_status = true};
    const fake_reply_t *reply;

    mgmt_setup_with(&options);
    reply = mgmt_get(mgmt_jar("admin"), STATUS_URL);
    mgmt_expect_error(reply, 503, "not_ready");
    mgmt_expect_retry_after(reply);
}

/* More project items than one document holds is a composition's fault, and it answers 503. */
static crux_status_t read_long_text(void *context, crux_mgmt_status_value_t *out)
{
    (void)context;
    out->type = CRUX_MGMT_STATUS_TEXT;
    memset(out->data.text, 'x', CRUX_MGMT_STATUS_TEXT_SIZE - 1u);
    return CRUX_OK;
}

static void test_a_report_that_does_not_fit_gets_503(void)
{
    static char names[CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX][CRUX_MGMT_STATUS_NAME_MAX + 1u];
    static crux_mgmt_status_item_t items[CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX];
    crux_mgmt_status_config_t config = {.platform_version = "1",
                                        .project_version = "1",
                                        .boot_reset_cause_status = CRUX_OK,
                                        .project_items = items,
                                        .project_item_count = CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX};
    mgmt_options_t options = {.status_config = &config};
    const fake_reply_t *reply;

    for (size_t index = 0; index < CRUX_MGMT_STATUS_PROJECT_ITEMS_MAX; ++index) {
        snprintf(names[index], sizeof(names[index]), "app.long_item_number_%02zu", index);
        items[index] = (crux_mgmt_status_item_t){names[index], "status.read", read_long_text, NULL};
    }
    mgmt_setup_with(&options);
    reply = mgmt_get(mgmt_jar("reader"), STATUS_URL);
    mgmt_expect_error(reply, 503, "not_ready");
    mgmt_expect_retry_after(reply);
    /* A session that sees none of them still gets its list, so the failure is the size. */
    mgmt_expect_body(mgmt_get(mgmt_jar("stranger"), STATUS_URL), 200, "{\"items\":[]}");
}

static void test_no_error_form_holds_a_path_or_native_text(void)
{
    const fake_reply_t *reply;

    mgmt_setup();
    reply = mgmt_anonymous(CRUX_WEB_GET, STATUS_URL, NULL);
    CHECK(strchr(reply->body, '/') == NULL);
    CHECK(strstr(reply->body, "errno") == NULL && strstr(reply->body, "ERR_") == NULL);
}

int main(void)
{
    mgmt_run_case("success", test_success);
    mgmt_run_case("each session sees its own items", test_each_session_sees_its_own_items);
    mgmt_run_case("unauthenticated", test_unauthenticated);
    mgmt_run_case("without a permission, an empty list",
                  test_a_session_without_a_permission_gets_an_empty_list_and_never_403);
    mgmt_run_case("a failed source hides nothing else",
                  test_a_failed_source_does_not_hide_the_rest);
    mgmt_run_case("invalid requests", test_invalid_requests);
    mgmt_run_case("not ready", test_not_ready);
    mgmt_run_case("a report that does not fit", test_a_report_that_does_not_fit_gets_503);
    mgmt_run_case("error forms hold no path", test_no_error_form_holds_a_path_or_native_text);
    return mgmt_finish("web_management_status");
}
