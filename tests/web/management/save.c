/* POST /api/v1/config/save and the worker behind it: success, no session, no permission, invalid
 * requests, one save at a time, each way a save can fail, and the real worker thread. */
#include "management_test.h"

#include <time.h>

#define LIST "/api/v1/config"
#define SAVE "/api/v1/config/save"
#define PENDING "{\"state\":\"pending\"}"

static bool has(const fake_reply_t *reply, const char *text)
{
    return strstr(reply->body, text) != NULL;
}

/* The save member of a list, as text. */
static const char *save_of(const fake_reply_t *reply)
{
    const char *at = strstr(reply->body, "\"save\":");

    return at == NULL ? "" : at;
}

static void edit_limit(const session_jar_t *jar, unsigned value)
{
    char body[32];

    snprintf(body, sizeof(body), "{\"value\":%u}", value);
    CHECK(mgmt_put(jar, LIST "/limit", body)->status == 200);
}

static void test_success(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;
    uint8_t stored[16];

    mgmt_setup();
    jar = mgmt_jar("admin");
    edit_limit(jar, 9u);
    CHECK(mgmt_unsaved() == 1u);
    reply = mgmt_post(jar, SAVE, NULL);
    mgmt_expect_body(reply, 202, PENDING);
    mgmt_expect_headers(reply, "application/json");
    /* Nothing is saved until the worker runs, and the list says so. */
    reply = mgmt_get(jar, LIST);
    CHECK(has(reply, ",\"save\":{\"state\":\"pending\"}}"));
    CHECK(has(reply, "\"name\":\"limit\""));
    CHECK(config_fault_writes() == 0u);
    CHECK(mgmt_unsaved() == 1u);
    CHECK(web_mgmt_work_once());
    CHECK(!web_mgmt_work_once());
    reply = mgmt_get(jar, LIST);
    CHECK(strcmp(save_of(reply), "\"save\":{\"state\":\"saved\"}}") == 0);
    CHECK(!has(reply, "\"unsaved\":true"));
    CHECK(mgmt_unsaved() == 0u);
    CHECK(config_fault_writes() == 1u);
    CHECK(config_fault_bytes(NAME_LIMIT, stored, sizeof(stored)) > 0u);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_SAVED) == 1u);
}

static void test_a_save_with_nothing_to_save_succeeds(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
    CHECK(strcmp(save_of(mgmt_get(jar, LIST)), "\"save\":{\"state\":\"saved\"}}") == 0);
    CHECK(config_fault_writes() == 0u);
}

static void test_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("admin");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_POST, SAVE, NULL), 401, "unauthorized");
    CHECK(!web_mgmt_work_once());
}

static void test_the_forgery_checks_come_with_the_session(void)
{
    const session_jar_t *jar;
    mgmt_variation_t no_token = {.no_token = true};
    mgmt_variation_t no_origin = {.no_origin = true};

    mgmt_setup();
    jar = mgmt_jar("admin");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_POST, SAVE, NULL, no_token), 403, "csrf_rejected");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_POST, SAVE, NULL, no_origin), 403, "origin_rejected");
    CHECK(!web_mgmt_work_once());
}

static void test_unpermitted(void)
{
    static const char *const users[] = {"reader", "writer", "both", "app", "stranger", "forced"};

    mgmt_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        mgmt_expect_error(mgmt_post(mgmt_jar(users[index]), SAVE, NULL), 403, "permission_denied");
    }
    /* Nothing was queued, and every refusal was a denial of config.save. */
    CHECK(!web_mgmt_work_once());
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 6u);
    /* config.save alone is enough, though the session lists nothing. */
    mgmt_expect_body(mgmt_post(mgmt_jar("saver"), SAVE, NULL), 202, PENDING);
}

static void test_invalid_requests(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    /* The route takes no body, and any body is over its limit of zero. */
    mgmt_expect_error(mgmt_post(jar, SAVE, "{}"), 413, "payload_too_large");
    mgmt_expect_error(mgmt_post(jar, SAVE, " "), 413, "payload_too_large");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_POST, SAVE, "{}"), 413, "payload_too_large");
    /* save is a fixed route with POST only, so another method gets the server's own answer. */
    mgmt_expect_plain(mgmt_get(jar, SAVE), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_put(jar, SAVE, "{\"value\":1}"), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_delete(jar, SAVE), 405, "Method Not Allowed");
    /* A path under it reaches the parameter route for a GET, which has no such parameter, and a
     * POST there has no resource with its method, so the server answers it. */
    mgmt_expect_error(mgmt_get(jar, SAVE "/deeper"), 404, "not_found");
    mgmt_expect_plain(mgmt_post(jar, SAVE "/deeper", NULL), 405, "Method Not Allowed");
    /* A query is not read. */
    mgmt_expect_body(mgmt_post(jar, SAVE "?force=1", NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
}

static void test_one_save_at_a_time(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;
    crux_web_management_stats_t stats;

    mgmt_setup();
    jar = mgmt_jar("admin");
    edit_limit(jar, 9u);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    reply = mgmt_post(jar, SAVE, NULL);
    mgmt_expect_error(reply, 503, "busy");
    mgmt_expect_retry_after(reply);
    /* Another account that holds config.save meets the same save. */
    mgmt_expect_error(mgmt_post(mgmt_jar("saver"), SAVE, NULL), 503, "busy");
    CHECK(web_mgmt_work_once());
    CHECK(!web_mgmt_work_once());
    /* Once it has ended, the next is accepted. */
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    crux_web_management_stats_get(&stats);
    CHECK(stats.saves_queued == 2u && stats.saves_busy == 2u && stats.saves_finished == 1u);
}

static const session_jar_t *running_jar;
static unsigned running_checked;

static void while_running(void *context)
{
    (void)context;
    /* The state says running, though the service is where the time goes. */
    CHECK(web_mgmt_save_view().state == WEB_SAVE_RUNNING);
    mgmt_expect_error(mgmt_post(running_jar, SAVE, NULL), 503, "busy");
    ++running_checked;
}

static void test_a_save_that_is_running_is_busy_too(void)
{
    mgmt_setup();
    running_jar = mgmt_jar("admin");
    running_checked = 0u;
    edit_limit(running_jar, 9u);
    mgmt_expect_body(mgmt_post(running_jar, SAVE, NULL), 202, PENDING);
    config_fault_write_hook(while_running, NULL);
    CHECK(web_mgmt_work_once());
    config_fault_write_hook(NULL, NULL);
    CHECK(running_checked >= 1u);
    CHECK(web_mgmt_save_view().state == WEB_SAVE_SAVED);
}

static void test_a_failed_save_is_reported_and_keeps_its_marks(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    mgmt_setup();
    jar = mgmt_jar("admin");
    edit_limit(jar, 9u);
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_NO_SPACE);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
    reply = mgmt_get(jar, LIST);
    CHECK(strcmp(save_of(reply), "\"save\":{\"state\":\"failed\",\"code\":\"storage_failed\"}}") ==
          0);
    /* The mark stays, and no name of a parameter is in the failure. */
    CHECK(has(reply, "\"unsaved\":true"));
    CHECK(mgmt_unsaved() == 1u);
    /* It stays until the next accepted save, which can succeed. */
    CHECK(has(mgmt_get(jar, LIST), "\"state\":\"failed\""));
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    CHECK(has(mgmt_get(jar, LIST), "\"state\":\"pending\""));
    CHECK(web_mgmt_work_once());
    CHECK(strcmp(save_of(mgmt_get(jar, LIST)), "\"save\":{\"state\":\"saved\"}}") == 0);
    CHECK(mgmt_unsaved() == 0u);
}

static void test_no_provider_is_not_supported(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    edit_limit(jar, 9u);
    config_fault_write_fail_key(NAME_LIMIT, CRUX_CONFIG_ERR_NOT_SUPPORTED);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
    CHECK(strcmp(save_of(mgmt_get(jar, LIST)),
                 "\"save\":{\"state\":\"failed\",\"code\":\"not_supported\"}}") == 0);
}

static void test_a_failure_in_a_parameter_the_session_cannot_see_names_no_parameter(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    mgmt_setup();
    jar = mgmt_jar("both");
    /* The application marked a parameter that nobody may see, and its save fails. */
    {
        crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 11u};

        CHECK(crux_config_service_try_update(NAME_PRIVATE, &value) == CRUX_CONFIG_SERVICE_OK);
    }
    config_fault_write_fail_key(NAME_PRIVATE, CRUX_CONFIG_ERR_IO);
    mgmt_expect_body(mgmt_post(mgmt_jar("admin"), SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
    reply = mgmt_get(mgmt_jar("admin"), LIST);
    CHECK(strcmp(save_of(reply), "\"save\":{\"state\":\"failed\",\"code\":\"storage_failed\"}}") ==
          0);
    CHECK(!has(reply, NAME_PRIVATE));
    (void)jar;
}

static void test_the_save_state_is_for_a_session_that_holds_config_save(void)
{
    const fake_reply_t *reply;

    mgmt_setup();
    mgmt_expect_body(mgmt_post(mgmt_jar("admin"), SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
    CHECK(has(mgmt_get(mgmt_jar("admin"), LIST), "\"save\":{\"state\":\"saved\"}"));
    /* A session that may list, and not save, never sees it. */
    CHECK(!has(mgmt_get(mgmt_jar("both"), LIST), "\"save\""));
    CHECK(!has(mgmt_get(mgmt_jar("reader"), LIST), "\"save\""));
    /* One that may save and list nothing sees it alone. */
    reply = mgmt_get(mgmt_jar("saver"), LIST);
    mgmt_expect_body(reply, 200, "{\"items\":[],\"save\":{\"state\":\"saved\"}}");
}

static void test_nothing_is_reported_before_a_save_was_requested(void)
{
    mgmt_setup();
    mgmt_expect_body(mgmt_get(mgmt_jar("saver"), LIST), 200, "{\"items\":[]}");
}

static void test_a_session_that_ended_before_the_worker_ran(void)
{
    const session_jar_t *jar;
    const session_jar_t *other;

    mgmt_setup();
    jar = mgmt_jar("saver");
    other = mgmt_jar("admin");
    edit_limit(other, 9u);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    CHECK(mgmt_delete(jar, "/api/v1/session")->status == 200);
    CHECK(web_mgmt_work_once());
    CHECK(strcmp(save_of(mgmt_get(other, LIST)),
                 "\"save\":{\"state\":\"failed\",\"code\":\"unauthorized\"}}") == 0);
    CHECK(config_fault_writes() == 0u);
    CHECK(mgmt_unsaved() == 1u);
}

/* The application's own save holds the service across storage, on another thread, and the web save,
 * which waits a finite time for the service, ends as busy. */
static void test_a_save_that_cannot_get_the_service_ends_as_busy(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    edit_limit(jar, 9u);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    mgmt_hold_service();
    CHECK(web_mgmt_work_once());
    mgmt_release_service();
    CHECK(strcmp(save_of(mgmt_get(jar, LIST)),
                 "\"save\":{\"state\":\"failed\",\"code\":\"busy\"}}") == 0);
    /* The client may ask again, and the application's save wrote the marks meanwhile. */
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_work_once());
    CHECK(strcmp(save_of(mgmt_get(jar, LIST)), "\"save\":{\"state\":\"saved\"}}") == 0);
}

static void test_not_ready(void)
{
    const mgmt_options_t options = {.no_management = true};
    const fake_reply_t *reply;

    mgmt_setup_with(&options);
    reply = mgmt_post(mgmt_jar("admin"), SAVE, NULL);
    mgmt_expect_error(reply, 503, "not_ready");
    mgmt_expect_retry_after(reply);
    CHECK(!web_mgmt_work_once());
    /* The list answers without the save state, since no save can have been requested. */
    CHECK(mgmt_get(mgmt_jar("admin"), LIST)->status == 200);
}

static void test_the_service_not_ready_ends_the_save_as_not_ready(void)
{
    const mgmt_options_t options = {.no_config = true};
    web_save_view_t view;

    mgmt_setup_with(&options);
    mgmt_expect_body(mgmt_post(mgmt_jar("admin"), SAVE, NULL), 202, PENDING);
    CHECK(web_mgmt_save_view().state == WEB_SAVE_QUEUED);
    CHECK(web_mgmt_work_once());
    view = web_mgmt_save_view();
    CHECK(view.state == WEB_SAVE_FAILED);
    CHECK(view.code != NULL && strcmp(view.code, "not_ready") == 0);
}

static void test_save_codes(void)
{
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_STORE, CRUX_CONFIG_ERR_IO),
                 "storage_failed") == 0);
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_STORE, CRUX_CONFIG_ERR_NOT_SUPPORTED),
                 "not_supported") == 0);
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_BUSY, CRUX_CONFIG_OK), "busy") == 0);
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_NOT_READY, CRUX_CONFIG_OK),
                 "not_ready") == 0);
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_SESSION_INVALID, CRUX_CONFIG_OK),
                 "unauthorized") == 0);
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_DENIED, CRUX_CONFIG_OK),
                 "permission_denied") == 0);
    CHECK(web_mgmt_save_code(CRUX_CONFIG_SERVICE_OK, CRUX_CONFIG_OK) == NULL);
    /* Anything else is the service's fault, and a client sees not_ready. */
    CHECK(strcmp(web_mgmt_save_code(CRUX_CONFIG_SERVICE_ERR_INVALID_ARGUMENT, CRUX_CONFIG_OK),
                 "not_ready") == 0);
}

/* The real worker thread. */
static void test_the_worker_thread_saves(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;
    struct timespec pause = {0, 1000000L};

    mgmt_setup();
    jar = mgmt_jar("admin");
    edit_limit(jar, 9u);
    CHECK(crux_web_management_start_worker() == CRUX_WEB_OK);
    CHECK(crux_web_management_start_worker() == CRUX_WEB_OK);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    reply = mgmt_get(jar, LIST);
    /* While the worker saves, the service lock is held, and a list is 503 busy and not pending. */
    for (unsigned waited = 0;
         waited < 5000u && (reply->status != 200 || has(reply, "\"state\":\"pending\""));
         ++waited) {
        (void)nanosleep(&pause, NULL);
        reply = mgmt_get(jar, LIST);
    }
    CHECK(strcmp(save_of(reply), "\"save\":{\"state\":\"saved\"}}") == 0);
    CHECK(mgmt_unsaved() == 0u);
    /* A second save after the first, through the same thread. */
    edit_limit(jar, 10u);
    mgmt_expect_body(mgmt_post(jar, SAVE, NULL), 202, PENDING);
    reply = mgmt_get(jar, LIST);
    /* While the worker saves, the service lock is held, and a list is 503 busy and not pending. */
    for (unsigned waited = 0;
         waited < 5000u && (reply->status != 200 || has(reply, "\"state\":\"pending\""));
         ++waited) {
        (void)nanosleep(&pause, NULL);
        reply = mgmt_get(jar, LIST);
    }
    CHECK(strcmp(save_of(reply), "\"save\":{\"state\":\"saved\"}}") == 0);
    CHECK(config_fault_writes() == 2u);
}

int main(void)
{
    mgmt_run_case("success", test_success);
    mgmt_run_case("nothing to save", test_a_save_with_nothing_to_save_succeeds);
    mgmt_run_case("unauthenticated", test_unauthenticated);
    mgmt_run_case("forgery checks", test_the_forgery_checks_come_with_the_session);
    mgmt_run_case("unpermitted", test_unpermitted);
    mgmt_run_case("invalid requests", test_invalid_requests);
    mgmt_run_case("one save at a time", test_one_save_at_a_time);
    mgmt_run_case("a running save is busy", test_a_save_that_is_running_is_busy_too);
    mgmt_run_case("a failed save", test_a_failed_save_is_reported_and_keeps_its_marks);
    mgmt_run_case("no provider", test_no_provider_is_not_supported);
    mgmt_run_case("a failure in a hidden parameter",
                  test_a_failure_in_a_parameter_the_session_cannot_see_names_no_parameter);
    mgmt_run_case("the save state is for config.save",
                  test_the_save_state_is_for_a_session_that_holds_config_save);
    mgmt_run_case("nothing before a save", test_nothing_is_reported_before_a_save_was_requested);
    mgmt_run_case("a session that ended", test_a_session_that_ended_before_the_worker_ran);
    mgmt_run_case("the service is held by the application",
                  test_a_save_that_cannot_get_the_service_ends_as_busy);
    mgmt_run_case("not ready", test_not_ready);
    mgmt_run_case("the service is not ready",
                  test_the_service_not_ready_ends_the_save_as_not_ready);
    mgmt_run_case("codes", test_save_codes);
    mgmt_run_case("the worker thread", test_the_worker_thread_saves);
    return mgmt_finish("web_management_save");
}
