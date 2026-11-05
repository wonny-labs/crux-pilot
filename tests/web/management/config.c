/* GET /api/v1/config, GET /api/v1/config/{name} and PUT /api/v1/config/{name}: success, no session,
 * a session without the permission, invalid input, and what a held lock and a missing service do.
 */
#include "management_test.h"

#define LIST "/api/v1/config"
#define ITEM(name, type, activation, sensitive, readable, writable, unsaved)                       \
    "{\"name\":\"" name "\",\"type\":\"" type "\",\"activation\":\"" activation                    \
    "\",\"sensitive\":" sensitive ",\"readable\":" readable ",\"writable\":" writable              \
    ",\"unsaved\":" unsaved "}"

static void expect_field(const fake_reply_t *reply, const char *code);

static bool has(const fake_reply_t *reply, const char *text)
{
    return strstr(reply->body, text) != NULL;
}

/* The list. */

static void test_list_for_a_session_that_may_read_and_write(void)
{
    const fake_reply_t *reply;

    mgmt_setup();
    reply = mgmt_get(mgmt_jar("both"), LIST);
    mgmt_expect_headers(reply, "application/json");
    mgmt_expect_body(
        reply, 200,
        "{\"items\":[" ITEM("mode", "bool", "application", "false", "true", "true", "false") "," ITEM(
            "limit", "u32", "application", "false", "true", "true",
            "false") "," ITEM("host_name", "string", "restart", "false", "true", "true",
                              "false") "," ITEM("small", "string", "application", "false", "true",
                                                "true",
                                                "false") "," ITEM("link_secret", "string",
                                                                  "restart", "true", "false",
                                                                  "true",
                                                                  "false") "," ITEM("fixed", "u32",
                                                                                    "safe_state",
                                                                                    "false", "true",
                                                                                    "false",
                                                                                    "false") "]}");
}

static void test_list_for_a_session_that_may_only_read(void)
{
    mgmt_setup();
    mgmt_expect_body(
        mgmt_get(mgmt_jar("reader"), LIST), 200,
        "{\"items\":[" ITEM("mode", "bool", "application", "false", "true", "false", "false") "," ITEM(
            "limit", "u32", "application", "false", "true", "false",
            "false") "," ITEM("host_name", "string", "restart", "false", "true", "false",
                              "false") "," ITEM("small", "string", "application", "false", "true",
                                                "false",
                                                "false") "," ITEM("link_secret", "string",
                                                                  "restart", "true", "false",
                                                                  "false",
                                                                  "false") "," ITEM("fixed", "u32",
                                                                                    "safe_state",
                                                                                    "false", "true",
                                                                                    "false",
                                                                                    "false") "]}");
}

static void test_list_for_a_session_that_may_only_write(void)
{
    mgmt_setup();
    /* It lists what it may write, none of it readable, and not the read-only parameter. */
    mgmt_expect_body(
        mgmt_get(mgmt_jar("writer"), LIST), 200,
        "{\"items\":[" ITEM(
            "mode", "bool", "application", "false", "false", "true",
            "false") "," ITEM("limit", "u32", "application", "false", "false", "true",
                              "false") "," ITEM("host_name", "string", "restart", "false", "false",
                                                "true",
                                                "false") "," ITEM("small", "string", "application",
                                                                  "false", "false", "true",
                                                                  "false") "," ITEM("link_secret",
                                                                                    "string",
                                                                                    "restart",
                                                                                    "true", "false",
                                                                                    "true",
                                                                                    "false") "]}");
}

static void test_list_holds_no_value_and_never_the_private_parameter(void)
{
    const fake_reply_t *reply;

    mgmt_setup();
    reply = mgmt_get(mgmt_jar("admin"), LIST);
    CHECK(reply->status == 200);
    CHECK(!has(reply, "\"value\""));
    CHECK(!has(reply, "private_key"));
    CHECK(!has(reply, "\"save\""));
    CHECK(!has(reply, "crux") && !has(reply, "unset"));
}

static void test_list_empty_for_a_session_that_may_see_nothing(void)
{
    static const char *const users[] = {"stranger", "app", "saver", "forced"};

    mgmt_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        const fake_reply_t *reply = mgmt_get(mgmt_jar(users[index]), LIST);

        CHECK(reply->status == 200);
        CHECK(strncmp(reply->body, "{\"items\":[]", 11) == 0);
    }
    /* A forced password change hides everything, and a hidden list is empty and not forbidden. */
    mgmt_expect_body(mgmt_get(mgmt_jar("forced"), LIST), 200, "{\"items\":[]}");
}

/* The list asks whether the session holds config.save with the quiet check. A session that does not
 * hold it is not refused anything, so no denial is recorded, and no idle timer is refreshed by a
 * read. */
static void test_a_list_records_no_denial_and_keeps_no_session_alive(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("both");
    access_fake_events_clear();
    CHECK(mgmt_get(jar, LIST)->status == 200);
    CHECK(mgmt_get(mgmt_jar("reader"), LIST)->status == 200);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 0u);
    /* Idle limit is 15 minutes. A list after 14 does not start the clock again. */
    session_clock_advance(UINT64_C(14) * 60u * 1000u);
    CHECK(mgmt_get(jar, LIST)->status == 200);
    session_clock_advance(UINT64_C(2) * 60u * 1000u);
    mgmt_expect_error(mgmt_get(jar, "/api/v1/session/me"), 401, "unauthorized");
}

static void test_list_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("admin");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_GET, LIST, NULL), 401, "unauthorized");
}

static void test_list_invalid_requests(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("admin");
    mgmt_expect_plain(mgmt_post(jar, LIST, NULL), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_delete(jar, LIST), 405, "Method Not Allowed");
    CHECK(mgmt_get(jar, LIST "?after=mode&limit=1")->status == 200);
    mgmt_expect_error(mgmt_get(jar, LIST "/"), 404, "not_found");
}

static void test_list_not_ready(void)
{
    const mgmt_options_t options = {.no_config = true};
    const fake_reply_t *reply;

    mgmt_setup_with(&options);
    reply = mgmt_get(mgmt_jar("admin"), LIST);
    mgmt_expect_error(reply, 503, "not_ready");
    mgmt_expect_retry_after(reply);
}

/* One parameter of the longest name each, in as many entries as a case asks for, to show where a
 * list stops fitting in one document. */
#define MANY_MAX CRUX_CONFIG_SERVICE_MAX

static crux_config_param_t many_params[MANY_MAX];
static crux_config_spec_t many_specs[MANY_MAX];
static char many_names[MANY_MAX][31];
static crux_config_service_entry_t many_table[MANY_MAX];
static size_t many_count;

static void many_prepare(void)
{
    for (size_t index = 0; index < many_count; ++index) {
        CHECK(crux_config_init(&many_params[index], &many_specs[index]) == CRUX_CONFIG_OK);
    }
}

static void many_case(void)
{
    mgmt_options_t options = {
        .table = many_table, .table_count = many_count, .prepare = many_prepare};
    const fake_reply_t *reply;
    /* {"items":[ and ]} around n items and n - 1 commas, against a body buffer of this size. */
    size_t item_length;
    size_t document_length;

    mgmt_setup_with(&options);
    reply = mgmt_get(mgmt_jar("both"), LIST);
    /* The item of the longest name has this many bytes, which the first entry shows. */
    item_length = strlen(ITEM("", "u32", "application", "false", "true", "true", "false")) + 30u;
    document_length = 12u + many_count * item_length + (many_count - 1u);
    if (document_length < CRUX_WEB_MANAGEMENT_BODY_MAX) {
        CHECK(reply->status == 200);
        CHECK(reply->body_length == document_length);
    } else {
        mgmt_expect_error(reply, 503, "not_ready");
        mgmt_expect_retry_after(reply);
    }
}

static void test_a_list_that_does_not_fit_one_document_gets_503(void)
{
    /* Every name is 30 characters, the longest a key can have. */
    for (size_t index = 0; index < MANY_MAX; ++index) {
        snprintf(many_names[index], sizeof(many_names[index]), "parameter_with_a_long_name__%02zu",
                 index);
        many_specs[index] = (crux_config_spec_t){
            many_names[index], 1, {.type = CRUX_CONFIG_U32, .data.u32 = 0u}, NULL, NULL};
        many_table[index] =
            (crux_config_service_entry_t){&many_params[index], "cfg.read", "cfg.write", false,
                                          CRUX_CONFIG_ACTIVATION_APPLICATION};
    }
    /* The case below runs once for each size around the edge, and for the whole table. */
    for (size_t count = 26u; count <= MANY_MAX; ++count) {
        many_count = count;
        mgmt_run_case("a table of this many long names", many_case);
    }
}

/* One parameter. */

static void test_get_each_type(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    mgmt_setup();
    jar = mgmt_jar("both");
    reply = mgmt_get(jar, LIST "/mode");
    mgmt_expect_headers(reply, "application/json");
    mgmt_expect_body(reply, 200, "{\"name\":\"mode\",\"type\":\"bool\",\"value\":false}");
    mgmt_expect_body(mgmt_get(jar, LIST "/limit"), 200,
                     "{\"name\":\"limit\",\"type\":\"u32\",\"value\":5}");
    mgmt_expect_body(mgmt_get(jar, LIST "/host_name"), 200,
                     "{\"name\":\"host_name\",\"type\":\"string\",\"value\":\"crux\"}");
    mgmt_expect_body(mgmt_get(jar, LIST "/small"), 200,
                     "{\"name\":\"small\",\"type\":\"string\",\"value\":\"\"}");
    mgmt_expect_body(mgmt_get(jar, LIST "/fixed"), 200,
                     "{\"name\":\"fixed\",\"type\":\"u32\",\"value\":7}");
    /* A reader reads the same, and a later change shows. */
    mgmt_expect_body(mgmt_get(mgmt_jar("reader"), LIST "/limit"), 200,
                     "{\"name\":\"limit\",\"type\":\"u32\",\"value\":5}");
}

static void test_get_escapes_a_string(void)
{
    crux_config_service_item_t applied;

    mgmt_setup();
    /* The service accepts quotes, backslashes and non-ASCII text, and the JSON must escape them. */
    CHECK(crux_config_service_update(mgmt_session("both"), NAME_HOST,
                                     "a\"b\\c \xc3\xa9\xe2\x82\xac", 11u,
                                     &applied) == CRUX_CONFIG_SERVICE_OK);
    mgmt_expect_body(mgmt_get(mgmt_jar("both"), LIST "/host_name"), 200,
                     "{\"name\":\"host_name\",\"type\":\"string\","
                     "\"value\":\"a\\\"b\\\\c \xc3\xa9\xe2\x82\xac\"}");
}

static void test_get_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("both");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_GET, LIST "/mode", NULL), 401, "unauthorized");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_GET, LIST "/nothing", NULL), 401, "unauthorized");
}

static void test_get_unpermitted(void)
{
    mgmt_setup();
    /* May write and not read: 403. May neither: the same 404 as a name that does not exist. */
    mgmt_expect_error(mgmt_get(mgmt_jar("writer"), LIST "/mode"), 403, "permission_denied");
    mgmt_expect_error(mgmt_get(mgmt_jar("stranger"), LIST "/mode"), 404, "not_found");
    mgmt_expect_error(mgmt_get(mgmt_jar("saver"), LIST "/mode"), 404, "not_found");
    mgmt_expect_error(mgmt_get(mgmt_jar("forced"), LIST "/mode"), 404, "not_found");
    mgmt_expect_error(mgmt_get(mgmt_jar("writer"), LIST "/fixed"), 404, "not_found");
    /* Sensitive is never readable, whoever asks, and the session that cannot see it gets 404. */
    mgmt_expect_error(mgmt_get(mgmt_jar("admin"), LIST "/link_secret"), 403, "permission_denied");
    mgmt_expect_error(mgmt_get(mgmt_jar("both"), LIST "/link_secret"), 403, "permission_denied");
    mgmt_expect_error(mgmt_get(mgmt_jar("stranger"), LIST "/link_secret"), 404, "not_found");
    /* Under a permission that no role holds. */
    mgmt_expect_error(mgmt_get(mgmt_jar("admin"), LIST "/private_key"), 404, "not_found");
}

static void test_get_a_hidden_and_a_missing_name_look_the_same(void)
{
    const fake_reply_t *hidden;
    char hidden_body[256];
    size_t denied;

    mgmt_setup();
    access_fake_events_clear();
    hidden = mgmt_get(mgmt_jar("admin"), LIST "/private_key");
    snprintf(hidden_body, sizeof(hidden_body), "%s", hidden->body);
    denied = access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED);
    /* A name that is not in the table records nothing, and a hidden one records the denial. */
    mgmt_expect_body(mgmt_get(mgmt_jar("admin"), LIST "/not_a_parameter"), 404, hidden_body);
    CHECK(denied == 1u);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == denied);
}

static void test_get_invalid_names(void)
{
    const session_jar_t *jar;
    char url[128];

    mgmt_setup();
    jar = mgmt_jar("admin");
    /* Longer than any key. */
    snprintf(url, sizeof(url), LIST "/%031d", 7);
    mgmt_expect_error(mgmt_get(jar, url), 404, "not_found");
    snprintf(url, sizeof(url), LIST "/%0100d", 7);
    mgmt_expect_error(mgmt_get(jar, url), 404, "not_found");
    /* Not unreserved characters, or dot segments: the core refuses them before the handler. */
    mgmt_expect_error(mgmt_get(jar, LIST "/mode%2fx"), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/%6dode"), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/mode%00"), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/.."), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/."), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/mode/deeper"), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/Mode"), 404, "not_found");
    mgmt_expect_error(mgmt_get(jar, LIST "/mode "), 404, "not_found");
    /* save is a fixed route with POST only, so the server answers a GET itself. */
    mgmt_expect_plain(mgmt_get(jar, LIST "/save"), 405, "Method Not Allowed");
    /* A query is not read. */
    CHECK(mgmt_get(jar, LIST "/mode?x=1")->status == 200);
    /* A body on a GET. */
    session_request_t with_body = {.method = CRUX_WEB_GET,
                                   .url = LIST "/mode",
                                   .host = SESSION_TEST_HOST,
                                   .cookie = jar->header,
                                   .content_type = "application/json",
                                   .body = "{}"};
    mgmt_expect_error(session_send(&with_body), 400, "bad_request");
}

static void test_get_not_ready(void)
{
    const mgmt_options_t options = {.no_config = true};

    mgmt_setup_with(&options);
    mgmt_expect_error(mgmt_get(mgmt_jar("admin"), LIST "/mode"), 503, "not_ready");
}

/* An update. */

static void expect_accepted(const fake_reply_t *reply, const char *item)
{
    mgmt_expect_body(reply, 200, item);
    mgmt_expect_headers(reply, "application/json");
    CHECK(reply->closed);
}

static void test_put_each_type(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("both");
    expect_accepted(mgmt_put(jar, LIST "/mode", "{\"value\":true}"),
                    ITEM("mode", "bool", "application", "false", "true", "true", "true"));
    mgmt_expect_bool(NAME_MODE, true);
    expect_accepted(mgmt_put(jar, LIST "/limit", "{\"value\": 10 }"),
                    ITEM("limit", "u32", "application", "false", "true", "true", "true"));
    mgmt_expect_u32(NAME_LIMIT, 10u);
    expect_accepted(mgmt_put(jar, LIST "/host_name", "{\"value\":\"Pump station 4\"}"),
                    ITEM("host_name", "string", "restart", "false", "true", "true", "true"));
    mgmt_expect_string(NAME_HOST, "Pump station 4");
    /* A string by escapes, and a u32 at the top of the validator's range. */
    CHECK(mgmt_put(jar, LIST "/host_name", "{\"value\":\"caf\\u00e9 \\ud83d\\ude00 \\\"q\\\"\"}")
              ->status == 200);
    mgmt_expect_string(NAME_HOST, "caf\xc3\xa9 \xf0\x9f\x98\x80 \"q\"");
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":100}")->status == 200);
    mgmt_expect_u32(NAME_LIMIT, 100u);
    /* A value that is the same as the old one is accepted, and marks the parameter unsaved. */
    CHECK(mgmt_put(jar, LIST "/mode", "{\"value\":true}")->status == 200);
    /* The list shows the marks, and a reader sees none of them. */
    CHECK(has(mgmt_get(jar, LIST),
              ITEM("mode", "bool", "application", "false", "true", "true", "true")));
    CHECK(!has(mgmt_get(mgmt_jar("reader"), LIST), "\"unsaved\":true"));
}

static void test_put_the_sensitive_value_is_written_and_never_returned(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    mgmt_setup();
    jar = mgmt_jar("both");
    reply = mgmt_put(jar, LIST "/link_secret", "{\"value\":\"SECRET-MARKER-4711\"}");
    expect_accepted(reply,
                    ITEM("link_secret", "string", "restart", "true", "false", "true", "true"));
    CHECK(!has(reply, "SECRET"));
    mgmt_expect_string(NAME_SECRET, "SECRET-MARKER-4711");
    mgmt_expect_error(mgmt_get(jar, LIST "/link_secret"), 403, "permission_denied");
    CHECK(!has(mgmt_get(jar, LIST), "SECRET"));
    /* Neither the handlers' buffers nor the core's hold the secret once the request is done. */
    CHECK(!web_management_routes_hold_bytes("SECRET-MARKER-4711", 18u));
    CHECK(!web_core_holds_bytes("SECRET-MARKER-4711", 18u));
}

static void test_put_leaves_no_scalar_text_behind(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("both");
    /* The text of a u32 goes through its own buffer, whether the update is accepted or refused. A
     * sensitive scalar would be a secret there, so the handlers' buffers must hold none of it
     * afterwards. The stack is not looked at: the reader's web_value_t holds the bool or u32 there,
     * and no test can see a stack. */
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":77}")->status == 200);
    CHECK(!web_management_routes_hold_bytes("77", 2u));
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":4711}")->status == 400);
    CHECK(!web_management_routes_hold_bytes("4711", 4u));
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":4711")->status == 400);
    CHECK(!web_management_routes_hold_bytes("4711", 4u));
    /* A string value is cleared with the rest. */
    CHECK(mgmt_put(jar, LIST "/host_name", "{\"value\":\"REFUSED-MARKER\"}")->status == 200);
    CHECK(!web_management_routes_hold_bytes("REFUSED-MARKER", 14u));
}

static void test_an_overlong_string_reaches_no_service_call(void)
{
    char long_text[CRUX_CONFIG_SERVICE_TEXT_SIZE + 8u];
    char body[CRUX_CONFIG_SERVICE_TEXT_SIZE + 32u];
    size_t rejected;

    mgmt_setup();
    memset(long_text, 'x', sizeof(long_text) - 1u);
    long_text[sizeof(long_text) - 1u] = '\0';
    snprintf(body, sizeof(body), "{\"value\":\"%s\"}", long_text);
    rejected = config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED);
    expect_field(mgmt_put(mgmt_jar("both"), LIST "/host_name", body), "rule_not_met");
    /* The handler refuses it on its own, so the service records no rejection and no update. */
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) == rejected);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 0u);
}

static void test_put_closes_the_connection_after_every_answer(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("both");
    CHECK(mgmt_put(jar, LIST "/mode", "{\"value\":true}")->closed);
    CHECK(mgmt_put(jar, LIST "/mode", "{\"value\":1}")->closed);
    CHECK(mgmt_put(jar, LIST "/mode", "{")->closed);
    CHECK(mgmt_put(jar, LIST "/private_key", "{\"value\":1}")->closed);
    CHECK(mgmt_put(mgmt_jar("reader"), LIST "/mode", "{\"value\":true}")->closed);
}

static void test_put_unauthenticated(void)
{
    mgmt_setup();
    (void)mgmt_jar("both");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_PUT, LIST "/mode", "{\"value\":true}"), 401,
                      "unauthorized");
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_PUT, LIST "/nothing", "{"), 401, "unauthorized");
    mgmt_expect_bool(NAME_MODE, false);
}

static void test_put_needs_the_forgery_checks(void)
{
    const session_jar_t *jar;
    mgmt_variation_t no_token = {.no_token = true};
    mgmt_variation_t no_origin = {.no_origin = true};

    mgmt_setup();
    jar = mgmt_jar("both");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/mode", "{\"value\":true}", no_token), 403,
                      "csrf_rejected");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/mode", "{\"value\":true}", no_origin),
                      403, "origin_rejected");
    mgmt_expect_bool(NAME_MODE, false);
}

static void test_put_unpermitted(void)
{
    const session_jar_t *reader;
    const session_jar_t *both;

    mgmt_setup();
    reader = mgmt_jar("reader");
    both = mgmt_jar("both");
    /* Visible and not writable: 403, whatever a well-formed body holds. */
    mgmt_expect_error(mgmt_put(reader, LIST "/mode", "{\"value\":true}"), 403, "permission_denied");
    mgmt_expect_error(mgmt_put(reader, LIST "/mode", "{\"value\":\"wrong type\"}"), 403,
                      "permission_denied");
    mgmt_expect_error(mgmt_put(reader, LIST "/limit", "{\"value\":4000000000}"), 403,
                      "permission_denied");
    mgmt_expect_error(mgmt_put(both, LIST "/fixed", "{\"value\":8}"), 403, "permission_denied");
    /* Not visible: 404, the same as for a name that does not exist. */
    mgmt_expect_error(mgmt_put(both, LIST "/private_key", "{\"value\":1}"), 404, "not_found");
    mgmt_expect_error(mgmt_put(both, LIST "/private_key", "{\"value\":\"wrong type\"}"), 404,
                      "not_found");
    mgmt_expect_error(mgmt_put(both, LIST "/nothing_here", "{\"value\":1}"), 404, "not_found");
    mgmt_expect_error(mgmt_put(mgmt_jar("stranger"), LIST "/mode", "{\"value\":true}"), 404,
                      "not_found");
    mgmt_expect_error(mgmt_put(mgmt_jar("forced"), LIST "/mode", "{\"value\":true}"), 404,
                      "not_found");
    mgmt_expect_error(mgmt_put(mgmt_jar("writer"), LIST "/fixed", "{\"value\":8}"), 404,
                      "not_found");
    /* A writer may write what it may not read. */
    CHECK(mgmt_put(mgmt_jar("writer"), LIST "/mode", "{\"value\":true}")->status == 200);
    mgmt_expect_bool(NAME_MODE, true);
    mgmt_expect_u32(NAME_FIXED, 7u);
    /* Only the one accepted update marked anything. */
    CHECK(mgmt_unsaved() == 1u);
}

static void expect_field(const fake_reply_t *reply, const char *code)
{
    char body[128];

    snprintf(body, sizeof(body),
             "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"value\",\"code\":\"%s\"}]}",
             code);
    mgmt_expect_body(reply, 400, body);
    CHECK(reply->closed);
}

static void test_put_invalid_values(void)
{
    const session_jar_t *jar;
    char long_text[CRUX_CONFIG_SERVICE_TEXT_SIZE + 40u];
    char body[CRUX_CONFIG_SERVICE_TEXT_SIZE + 64u];

    mgmt_setup();
    jar = mgmt_jar("both");
    /* The JSON type is not the parameter's. */
    expect_field(mgmt_put(jar, LIST "/mode", "{\"value\":\"true\"}"), "wrong_type");
    expect_field(mgmt_put(jar, LIST "/mode", "{\"value\":1}"), "wrong_type");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":\"5\"}"), "wrong_type");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":true}"), "wrong_type");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":7}"), "wrong_type");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":false}"), "wrong_type");
    /* A number that is not a u32. */
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":-1}"), "out_of_range");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":4294967296}"), "out_of_range");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":99999999999999999999999999}"),
                 "out_of_range");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":1.5}"), "out_of_range");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":5.0}"), "out_of_range");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":1e1}"), "out_of_range");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":-0}"), "out_of_range");
    /* A validator, the character rule and the capacity, which give no reason. */
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":0}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":101}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/limit", "{\"value\":4294967295}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":\"a bad name\"}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":\"tab\\tinside\"}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":\"nul\\u0000inside\"}"),
                 "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":\"del\x7f\"}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":\"c1 \\u0085 control\"}"),
                 "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/host_name", "{\"value\":\"bad utf8 \xff\"}"), "rule_not_met");
    expect_field(mgmt_put(jar, LIST "/small", "{\"value\":\"123456789\"}"), "rule_not_met");
    /* The longest string that the service holds is accepted, and one more byte is not. */
    memset(long_text, 'x', CRUX_CONFIG_STRING_MAX);
    long_text[CRUX_CONFIG_STRING_MAX] = '\0';
    snprintf(body, sizeof(body), "{\"value\":\"%s\"}", long_text);
    CHECK(mgmt_put(jar, LIST "/host_name", body)->status == 200);
    long_text[CRUX_CONFIG_STRING_MAX] = 'x';
    long_text[CRUX_CONFIG_STRING_MAX + 1u] = '\0';
    snprintf(body, sizeof(body), "{\"value\":\"%s\"}", long_text);
    expect_field(mgmt_put(jar, LIST "/host_name", body), "rule_not_met");
    /* None of the refusals changed a value or marked anything beyond the one that was accepted. */
    mgmt_expect_bool(NAME_MODE, false);
    mgmt_expect_u32(NAME_LIMIT, 5u);
    mgmt_expect_string(NAME_SMALL, "");
    CHECK(mgmt_unsaved() == 1u);
}

static void test_put_malformed_bodies(void)
{
    static const char *const bodies[] = {
        "",
        "{",
        "}",
        "{}",
        "[]",
        "true",
        "\"value\"",
        "{\"value\"}",
        "{\"value\":}",
        "{\"value\":1",
        "{\"value\":1,}",
        "{\"value\":1}}",
        "{\"value\":1} x",
        "{\"value\":1}{}",
        "{\"value\":null}",
        "{\"value\":[1]}",
        "{\"value\":{\"a\":1}}",
        "{\"value\":[]}",
        "{\"value\":1,\"value\":2}",
        "{\"value\":1,\"other\":2}",
        "{\"other\":1}",
        "{\"Value\":1}",
        "{\"valu\":1}",
        "{\"values\":1}",
        "{'value':1}",
        "{value:1}",
        "{\"value\":01}",
        "{\"value\":+1}",
        "{\"value\":.5}",
        "{\"value\":1.}",
        "{\"value\":1e}",
        "{\"value\":0x10}",
        "{\"value\":NaN}",
        "{\"value\":Infinity}",
        "{\"value\":tru}",
        "{\"value\":True}",
        "{\"value\":truex}",
        "{\"value\":\"unterminated}",
        "{\"value\":\"bad \\x escape\"}",
        "{\"value\":\"\\u12\"}",
        "{\"value\":\"\\u12zz\"}",
        "{\"value\":\"\\ud800\"}",
        "{\"value\":\"\\udc00\"}",
        "{\"value\":\"\\ud800\\u0041\"}",
        "{\"value\":\"raw\ttab\"}",
        "{\"value\":\"raw\nnewline\"}",
        "\xef\xbb\xbf{\"value\":1}",
        "{\"value\":1}\x01",
    };
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("both");
    for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        const fake_reply_t *reply = mgmt_put(jar, LIST "/limit", bodies[index]);

        if (reply->status != 400) {
            printf("  body %zu: %s -> %d\n", index, bodies[index], reply->status);
        }
        mgmt_expect_error(reply, 400, "bad_request");
        CHECK(reply->closed);
    }
    /* Whitespace around the pieces is JSON, and is accepted. */
    CHECK(mgmt_put(jar, LIST "/limit", " \t\r\n{ \"value\" \n:\t 12 \r}\n ")->status == 200);
    mgmt_expect_u32(NAME_LIMIT, 12u);
    CHECK(mgmt_unsaved() == 1u);
}

static void test_put_needs_a_json_content_type(void)
{
    const session_jar_t *jar;
    mgmt_variation_t none = {.content_type = ""};
    mgmt_variation_t text = {.content_type = "text/plain"};
    mgmt_variation_t form = {.content_type = "application/x-www-form-urlencoded"};
    mgmt_variation_t near = {.content_type = "application/jsonx"};
    mgmt_variation_t upper = {.content_type = "Application/JSON; charset=utf-8"};

    mgmt_setup();
    jar = mgmt_jar("both");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/limit", "{\"value\":9}", none), 400,
                      "bad_request");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/limit", "{\"value\":9}", text), 400,
                      "bad_request");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/limit", "{\"value\":9}", form), 400,
                      "bad_request");
    mgmt_expect_error(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/limit", "{\"value\":9}", near), 400,
                      "bad_request");
    mgmt_expect_u32(NAME_LIMIT, 5u);
    CHECK(mgmt_vary(jar, CRUX_WEB_PUT, LIST "/limit", "{\"value\":9}", upper)->status == 200);
}

static void test_put_oversized_bodies(void)
{
    static char body[CRUX_WEB_BODY_BUFFER + 512u];
    const session_jar_t *jar;
    size_t length;

    mgmt_setup();
    jar = mgmt_jar("both");
    /* A body of exactly the limit reaches the handler, and is malformed. */
    memset(body, ' ', sizeof(body));
    memcpy(body, "{\"value\":1}", 11u);
    body[CRUX_WEB_BODY_BUFFER] = '\0';
    CHECK(mgmt_put(jar, LIST "/limit", body)->status == 200);
    body[CRUX_WEB_BODY_BUFFER] = ' ';
    body[CRUX_WEB_BODY_BUFFER + 1u] = '\0';
    mgmt_expect_error(mgmt_put(jar, LIST "/limit", body), 413, "payload_too_large");
    for (length = CRUX_WEB_BODY_BUFFER + 2u; length < sizeof(body); length += 97u) {
        body[length] = '\0';
        mgmt_expect_error(mgmt_put(jar, LIST "/limit", body), 413, "payload_too_large");
        body[length] = ' ';
    }
    /* The limit is checked before the session is, so a request with no session gets it too. */
    mgmt_expect_error(mgmt_anonymous(CRUX_WEB_PUT, LIST "/limit", body), 413, "payload_too_large");
    /* A huge string the service could never hold. */
    memset(body, 'x', sizeof(body) - 1u);
    body[sizeof(body) - 1u] = '\0';
    mgmt_expect_error(mgmt_put(jar, LIST "/host_name", body), 413, "payload_too_large");
    mgmt_expect_u32(NAME_LIMIT, 1u);
    mgmt_expect_string(NAME_HOST, "crux");
}

static void test_put_invalid_names(void)
{
    const session_jar_t *jar;
    char url[160];

    mgmt_setup();
    jar = mgmt_jar("both");
    snprintf(url, sizeof(url), LIST "/%031d", 7);
    mgmt_expect_error(mgmt_put(jar, url, "{\"value\":1}"), 404, "not_found");
    mgmt_expect_error(mgmt_put(jar, LIST "/..", "{\"value\":1}"), 404, "not_found");
    mgmt_expect_error(mgmt_put(jar, LIST "/mode%2f", "{\"value\":1}"), 404, "not_found");
    mgmt_expect_error(mgmt_put(jar, LIST "/limit/deeper", "{\"value\":1}"), 404, "not_found");
    /* save is a fixed route with POST only. */
    mgmt_expect_plain(mgmt_put(jar, LIST "/save", "{\"value\":1}"), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_post(jar, LIST "/mode", "{\"value\":true}"), 405, "Method Not Allowed");
    mgmt_expect_plain(mgmt_delete(jar, LIST "/mode"), 405, "Method Not Allowed");
    /* A query is not read. */
    CHECK(mgmt_put(jar, LIST "/limit?value=99", "{\"value\":7}")->status == 200);
    mgmt_expect_u32(NAME_LIMIT, 7u);
}

static void test_put_events(void)
{
    const session_jar_t *jar;

    mgmt_setup();
    jar = mgmt_jar("both");
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":9}")->status == 200);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 1u);
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":0}")->status == 400);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) == 1u);
    /* A request that never reached the service leaves no event. */
    CHECK(mgmt_put(jar, LIST "/limit", "{\"value\":\"x\"}")->status == 400);
    CHECK(mgmt_put(jar, LIST "/limit", "{")->status == 400);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_REJECTED) == 1u);
    CHECK(config_events_of(CRUX_CONFIG_SERVICE_EVENT_UPDATED) == 1u);
    /* An update of what the session cannot see reaches the service, which records the denial. A
     * name that is not in the table records nothing, because it is a client's text. */
    access_fake_events_clear();
    CHECK(mgmt_put(jar, LIST "/private_key", "{\"value\":1}")->status == 404);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 1u);
    CHECK(mgmt_put(jar, LIST "/nothing_here", "{\"value\":1}")->status == 404);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 1u);
    /* A read-only parameter that the session can see is refused by the service with no denial. */
    CHECK(mgmt_put(jar, LIST "/fixed", "{\"value\":1}")->status == 403);
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 1u);
}

static void test_put_not_ready(void)
{
    const mgmt_options_t options = {.no_config = true};

    mgmt_setup_with(&options);
    mgmt_expect_error(mgmt_put(mgmt_jar("both"), LIST "/mode", "{\"value\":true}"), 503,
                      "not_ready");
}

/* A held lock, while the application's own save is inside storage on another thread. */
static void test_a_held_lock_answers_busy(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    mgmt_setup();
    jar = mgmt_jar("admin");
    mgmt_hold_service();
    reply = mgmt_get(jar, LIST);
    mgmt_expect_error(reply, 503, "busy");
    mgmt_expect_retry_after(reply);
    reply = mgmt_get(jar, LIST "/mode");
    mgmt_expect_error(reply, 503, "busy");
    mgmt_expect_retry_after(reply);
    reply = mgmt_put(jar, LIST "/mode", "{\"value\":true}");
    mgmt_expect_error(reply, 503, "busy");
    mgmt_expect_retry_after(reply);
    /* A path that never reaches the service is still answered. */
    mgmt_expect_error(mgmt_get(jar, LIST "/.."), 404, "not_found");
    mgmt_expect_body(
        mgmt_get(jar, "/api/v1/session/me"), 200,
        "{\"user\":\"admin\",\"role\":\"administrator\",\"must_change_password\":false}");
    mgmt_release_service();
    /* The refusals changed nothing, and afterwards the same requests are answered. */
    mgmt_expect_bool(NAME_MODE, false);
    CHECK(mgmt_get(jar, LIST "/mode")->status == 200);
    CHECK(mgmt_put(jar, LIST "/mode", "{\"value\":true}")->status == 200);
}

int main(void)
{
    mgmt_run_case("list: read and write", test_list_for_a_session_that_may_read_and_write);
    mgmt_run_case("list: read only", test_list_for_a_session_that_may_only_read);
    mgmt_run_case("list: write only", test_list_for_a_session_that_may_only_write);
    mgmt_run_case("list: no value and no private parameter",
                  test_list_holds_no_value_and_never_the_private_parameter);
    mgmt_run_case("list: empty", test_list_empty_for_a_session_that_may_see_nothing);
    mgmt_run_case("list: no denial, no idle refresh",
                  test_a_list_records_no_denial_and_keeps_no_session_alive);
    mgmt_run_case("list: unauthenticated", test_list_unauthenticated);
    mgmt_run_case("list: invalid requests", test_list_invalid_requests);
    mgmt_run_case("list: not ready", test_list_not_ready);
    test_a_list_that_does_not_fit_one_document_gets_503();
    mgmt_run_case("get: each type", test_get_each_type);
    mgmt_run_case("get: escapes a string", test_get_escapes_a_string);
    mgmt_run_case("get: unauthenticated", test_get_unauthenticated);
    mgmt_run_case("get: unpermitted", test_get_unpermitted);
    mgmt_run_case("get: hidden and missing", test_get_a_hidden_and_a_missing_name_look_the_same);
    mgmt_run_case("get: invalid names", test_get_invalid_names);
    mgmt_run_case("get: not ready", test_get_not_ready);
    mgmt_run_case("put: each type", test_put_each_type);
    mgmt_run_case("put: a sensitive value",
                  test_put_the_sensitive_value_is_written_and_never_returned);
    mgmt_run_case("put: no scalar text left", test_put_leaves_no_scalar_text_behind);
    mgmt_run_case("put: an overlong string", test_an_overlong_string_reaches_no_service_call);
    mgmt_run_case("put: closes the connection", test_put_closes_the_connection_after_every_answer);
    mgmt_run_case("put: unauthenticated", test_put_unauthenticated);
    mgmt_run_case("put: forgery checks", test_put_needs_the_forgery_checks);
    mgmt_run_case("put: unpermitted", test_put_unpermitted);
    mgmt_run_case("put: invalid values", test_put_invalid_values);
    mgmt_run_case("put: malformed bodies", test_put_malformed_bodies);
    mgmt_run_case("put: content type", test_put_needs_a_json_content_type);
    mgmt_run_case("put: oversized bodies", test_put_oversized_bodies);
    mgmt_run_case("put: invalid names", test_put_invalid_names);
    mgmt_run_case("put: events", test_put_events);
    mgmt_run_case("put: not ready", test_put_not_ready);
    mgmt_run_case("a held lock answers busy", test_a_held_lock_answers_busy);
    return mgmt_finish("web_management_config");
}
