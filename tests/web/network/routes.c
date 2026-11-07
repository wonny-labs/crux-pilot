/* GET and PUT /api/v1/network: success, no session, no permission, invalid input, one save at a
 * time, a failed save, and what the answers carry. On the real core, services and Access. */
#include "network_web.h"

#define DEFAULT_RUNNING "\"running\":{\"address\":\"192.0.2.10\",\"prefix_length\":24}"
#define DEFAULTS "{" DEFAULT_RUNNING ",\"source\":\"default\",\"reboot_required\":false}"
#define SET_A_JSON                                                                                 \
    "{\"address\":\"198.51.100.20\",\"prefix_length\":24,\"gateway\":\"198.51.100.1\"}"
#define SET_B_JSON "{\"address\":\"203.0.113.20\",\"prefix_length\":26}"
#define PENDING "{\"state\":\"pending\"}"

static bool has(const fake_reply_t *reply, const char *text)
{
    return strstr(reply->body, text) != NULL;
}

static void test_get_success(void)
{
    static const char *const users[] = {"admin", "reader", "both"};

    web_net_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        const fake_reply_t *reply = web_net_get(web_net_jar(users[index]));

        web_net_expect_body(reply, 200, DEFAULTS);
        web_net_expect_headers(reply, "application/json");
    }
    CHECK(net_events_count() == 0u);
}

static void test_get_without_an_address(void)
{
    web_net_setup();
    net_port_set_running(false, NULL);
    web_net_expect_body(web_net_get(web_net_jar("reader")), 200,
                        "{\"source\":\"default\",\"reboot_required\":false}");
}

static void test_get_after_boot_on_saved_values(void)
{
    config_fault_reset();
    net_store_set(&net_set_a, true);
    web_net_setup_on_store();
    web_net_expect_body(web_net_get(web_net_jar("reader")), 200,
                        "{\"running\":" SET_A_JSON ",\"saved\":" SET_A_JSON ",\"source\":\"saved\","
                        "\"reboot_required\":false}");
}

static void test_get_unauthenticated(void)
{
    web_net_setup();
    (void)web_net_jar("reader");
    web_net_expect_error(web_net_anonymous(CRUX_WEB_GET, NULL), 401, "unauthorized");
}

static void test_get_unpermitted(void)
{
    static const char *const users[] = {"writer", "saver", "stranger", "forced"};

    web_net_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        web_net_expect_error(web_net_get(web_net_jar(users[index])), 403, "permission_denied");
    }
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 4u);
}

static void test_put_success(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_net_setup();
    jar = web_net_jar("both");
    reply = web_net_put(jar, SET_A_JSON);
    web_net_expect_body(reply, 202, PENDING);
    web_net_expect_headers(reply, "application/json");
    CHECK(reply->closed);
    /* Accepted is not saved: nothing is written until the worker runs, and GET says so. */
    CHECK(config_fault_writes() == 0u);
    web_net_expect_body(web_net_get(jar), 200,
                        "{" DEFAULT_RUNNING ",\"source\":\"default\",\"reboot_required\":false,"
                        "\"save\":{\"state\":\"pending\"}}");
    CHECK(crux_network_settings_work_once());
    web_net_expect_body(web_net_get(jar), 200,
                        "{" DEFAULT_RUNNING ",\"saved\":" SET_A_JSON
                        ",\"source\":\"default\",\"reboot_required\":true}");
    CHECK(config_fault_write_log_count() == 5u);
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 1u);
    /* No gateway, and a white-space body. */
    web_net_expect_body(
        web_net_put(jar, " \n{ \"address\" : \"203.0.113.20\" ,\r\n \"prefix_length\":26 }\t"), 202,
        PENDING);
    CHECK(crux_network_settings_work_once());
    web_net_expect_body(web_net_get(jar), 200,
                        "{" DEFAULT_RUNNING ",\"saved\":{\"address\":\"203.0.113.20\","
                        "\"prefix_length\":26},\"source\":\"default\",\"reboot_required\":true}");
}

static void test_a_write_only_session_may_save(void)
{
    web_net_setup();
    web_net_expect_body(web_net_put(web_net_jar("writer"), SET_B_JSON), 202, PENDING);
    CHECK(crux_network_settings_work_once());
}

static void test_put_unauthenticated(void)
{
    web_net_setup();
    (void)web_net_jar("both");
    web_net_expect_error(web_net_anonymous(CRUX_WEB_PUT, SET_A_JSON), 401, "unauthorized");
    CHECK(!crux_network_settings_work_once());
}

static void test_the_forgery_checks_come_with_the_session(void)
{
    const session_jar_t *jar;
    web_net_variation_t no_token = {.no_token = true};
    web_net_variation_t no_origin = {.no_origin = true};

    web_net_setup();
    jar = web_net_jar("both");
    web_net_expect_error(web_net_vary(jar, CRUX_WEB_PUT, SET_A_JSON, no_token), 403,
                         "csrf_rejected");
    web_net_expect_error(web_net_vary(jar, CRUX_WEB_PUT, SET_A_JSON, no_origin), 403,
                         "origin_rejected");
    CHECK(!crux_network_settings_work_once());
}

static void test_put_unpermitted(void)
{
    static const char *const users[] = {"reader", "saver", "stranger", "forced"};

    web_net_setup();
    for (size_t index = 0; index < sizeof(users) / sizeof(users[0]); ++index) {
        web_net_expect_error(web_net_put(web_net_jar(users[index]), SET_A_JSON), 403,
                             "permission_denied");
    }
    CHECK(!crux_network_settings_work_once());
    CHECK(access_fake_event_count_of(CRUX_ACCESS_EVENT_PERMISSION_DENIED) == 4u);
    CHECK(config_fault_writes() == 0u && net_events_count() == 0u);
}

static void test_invalid_values_get_field_errors(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    web_net_expect_body(
        web_net_put(
            jar, "{\"address\":\"224.0.0.1\",\"prefix_length\":31,\"gateway\":\"198.51.100.1\"}"),
        400,
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"address\",\"code\":\"not_unicast\"},"
        "{\"name\":\"prefix_length\",\"code\":\"prefix_not_usable\"}]}");
    web_net_expect_body(
        web_net_put(
            jar, "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":\"198.51.100.1\"}"),
        400,
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"gateway\","
        "\"code\":\"gateway_outside_subnet\"}]}");
    /* Each rule of the gateway has the one code that the specification holds. */
    static const char *const gateways[] = {"0.0.0.0", "192.0.2.20", "192.0.2.0", "192.0.2.255"};
    for (size_t index = 0; index < sizeof(gateways) / sizeof(gateways[0]); ++index) {
        char body[160];

        snprintf(body, sizeof(body),
                 "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":\"%s\"}",
                 gateways[index]);
        web_net_expect_body(web_net_put(jar, body), 400,
                            "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"gateway\","
                            "\"code\":\"gateway_outside_subnet\"}]}");
    }
    web_net_expect_body(web_net_put(jar, "{\"address\":\"192.0.2.0\",\"prefix_length\":24}"), 400,
                        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"address\",\"code\":"
                        "\"not_unicast\"}]}");
    web_net_expect_body(web_net_put(jar, "{\"address\":\"192.0.2.255\",\"prefix_length\":24}"), 400,
                        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"address\",\"code\":"
                        "\"not_unicast\"}]}");
    /* A negative, a large and an enormous prefix length are values, not malformed bodies. */
    static const char *const prefixes[] = {"0",  "31",         "32",
                                           "-1", "4294967295", "99999999999999999999"};
    for (size_t index = 0; index < sizeof(prefixes) / sizeof(prefixes[0]); ++index) {
        char body[160];

        snprintf(body, sizeof(body), "{\"address\":\"192.0.2.20\",\"prefix_length\":%s}",
                 prefixes[index]);
        web_net_expect_body(web_net_put(jar, body), 400,
                            "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"prefix_length\","
                            "\"code\":\"prefix_not_usable\"}]}");
    }
    /* Every refusal changed nothing. */
    CHECK(!crux_network_settings_work_once());
    CHECK(config_fault_writes() == 0u && net_events_count() == 0u);
    web_net_expect_body(web_net_get(jar), 200, DEFAULTS);
}

static void test_malformed_bodies(void)
{
    /* One body to a line, however long: the list is read as data. */
    // clang-format off
    static const char *const bodies[] = {
        "",
        "{",
        "}",
        "[]",
        "null",
        "{}",
        "{\"address\":\"192.0.2.20\"}",
        "{\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24} x",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24}{}",
        "{\"address\":\"192.0.2.20\",\"address\":\"192.0.2.21\",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"prefix_length\":25}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":\"192.0.2.1\",\"gateway\":\"192.0.2.2\"}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"mtu\":1500}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"note\":\"x\"}",
        "{\"address\\u0000x\":\"192.0.2.20\",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":null}",
        "{\"address\":null,\"prefix_length\":24}",
        "{\"address\":192,\"prefix_length\":24}",
        "{\"address\":[\"192.0.2.20\"],\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":\"24\"}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24.0}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":2.4e1}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":1e1}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":024}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":+24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":true}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":{}}",
        "{\"address\":\"192.0.2\",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.256\",\"prefix_length\":24}",
        "{\"address\":\"192.000.2.20\",\"prefix_length\":24}",
        "{\"address\":\" 192.0.2.20\",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20 \",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":\"x\"}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"gateway\":\"\"}",
        "{\"address\":\"\\u00e9.0.2.20\",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.2\\u0000\",\"prefix_length\":24}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24,\"address\\u0000x\":1}",
        "{'address':'192.0.2.20','prefix_length':24}",
        "{address:\"192.0.2.20\",prefix_length:24}",
    };
    // clang-format on
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
        const fake_reply_t *reply = web_net_put(jar, bodies[index]);

        if (reply->status != 400) {
            printf("  accepted: %s\n", bodies[index]);
        }
        web_net_expect_error(reply, 400, "bad_request");
    }
    CHECK(!crux_network_settings_work_once());
    CHECK(config_fault_writes() == 0u);
    /* Escapes of ASCII are fine. */
    web_net_expect_body(
        web_net_put(jar, "{\"\\u0061ddress\":\"192\\u002e0.2.20\",\"prefix_length\":24}"), 202,
        PENDING);
}

static void test_content_type_and_size(void)
{
    const session_jar_t *jar;
    web_net_variation_t other = {.content_type = "text/plain"};
    web_net_variation_t none = {.content_type = ""};
    web_net_variation_t upper = {.content_type = "Application/JSON; charset=utf-8"};
    web_net_variation_t near_miss = {.content_type = "application/jsonp"};
    char big[WEB_MGMT_NETWORK_BODY_MAX + 8u];

    web_net_setup();
    jar = web_net_jar("both");
    web_net_expect_error(web_net_vary(jar, CRUX_WEB_PUT, SET_A_JSON, other), 400, "bad_request");
    web_net_expect_error(web_net_vary(jar, CRUX_WEB_PUT, SET_A_JSON, none), 400, "bad_request");
    web_net_expect_error(web_net_vary(jar, CRUX_WEB_PUT, SET_A_JSON, near_miss), 400,
                         "bad_request");
    web_net_expect_body(web_net_vary(jar, CRUX_WEB_PUT, SET_A_JSON, upper), 202, PENDING);
    CHECK(crux_network_settings_work_once());

    /* The limit is the route's, and the core refuses before any session lookup. */
    memset(big, ' ', sizeof(big));
    memcpy(big, SET_B_JSON, sizeof(SET_B_JSON) - 1u);
    big[WEB_MGMT_NETWORK_BODY_MAX] = '\0';
    web_net_expect_body(web_net_put(jar, big), 202, PENDING);
    big[WEB_MGMT_NETWORK_BODY_MAX] = ' ';
    big[WEB_MGMT_NETWORK_BODY_MAX + 1u] = '\0';
    web_net_expect_error(web_net_put(jar, big), 413, "payload_too_large");
    web_net_expect_error(web_net_anonymous(CRUX_WEB_PUT, big), 413, "payload_too_large");
}

static void test_other_methods(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_net_setup();
    jar = web_net_jar("admin");
    /* The path has GET and PUT. The server answers any other method itself. */
    reply = session_with(jar, CRUX_WEB_POST, NETWORK_URL, "{}");
    CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 405);
    reply = session_with(jar, CRUX_WEB_DELETE, NETWORK_URL, NULL);
    CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 405);
    /* A path under it, and a query, are other routes or none. */
    reply = session_with(jar, CRUX_WEB_GET, NETWORK_URL "/x", NULL);
    if (reply->status != 404) {
        printf("  %d %s\n", reply->status, reply->body);
    }
    CHECK(reply->status == 404);
    web_net_expect_body(session_with(jar, CRUX_WEB_GET, NETWORK_URL "?a=1", NULL), 200, DEFAULTS);
}

static void test_one_save_at_a_time(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_net_setup();
    jar = web_net_jar("both");
    web_net_expect_body(web_net_put(jar, SET_A_JSON), 202, PENDING);
    reply = web_net_put(jar, SET_B_JSON);
    web_net_expect_error(reply, 503, "busy");
    web_net_expect_retry_after(reply);
    CHECK(reply->closed);
    CHECK(crux_network_settings_work_once());
    /* The first one is what was saved, and the second changed nothing. */
    CHECK(has(web_net_get(jar), "\"address\":\"198.51.100.20\""));
    web_net_expect_body(web_net_put(jar, SET_B_JSON), 202, PENDING);
}

static void test_a_failed_save(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    config_fault_write_fail_key(CRUX_NETWORK_SETTINGS_KEY_CHECK, CRUX_CONFIG_ERR_NO_SPACE);
    web_net_expect_body(web_net_put(jar, SET_A_JSON), 202, PENDING);
    CHECK(crux_network_settings_work_once());
    /* The stored set no longer passes its check, so none is shown as saved, and a client is told
     * that the save failed, and not why. */
    web_net_expect_body(web_net_get(jar), 200,
                        "{" DEFAULT_RUNNING ",\"source\":\"default\",\"reboot_required\":false,"
                        "\"save\":{\"state\":\"failed\"}}");
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED) == 1u);
    /* The next accepted request clears it. */
    config_fault_write_fail_key(NULL, CRUX_CONFIG_OK);
    web_net_expect_body(web_net_put(jar, SET_A_JSON), 202, PENDING);
    CHECK(!has(web_net_get(jar), "\"failed\""));
    CHECK(crux_network_settings_work_once());
    CHECK(has(web_net_get(jar), "\"saved\":"));
}

static void test_the_connection_closes_after_every_put_answer(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    CHECK(web_net_put(jar, SET_A_JSON)->closed);
    CHECK(web_net_put(jar, SET_B_JSON)->closed);
    CHECK(web_net_put(jar, "{")->closed);
    CHECK(web_net_put(jar, "{\"address\":\"224.0.0.1\",\"prefix_length\":24}")->closed);
    CHECK(web_net_put(web_net_jar("reader"), SET_A_JSON)->closed);
}

static void test_an_ended_session_is_a_401(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    session_clock_advance(9ull * 3600ull * 1000ull);
    web_net_expect_error(web_net_get(jar), 401, "unauthorized");
    web_net_expect_error(web_net_put(jar, SET_A_JSON), 401, "unauthorized");
}

int main(void)
{
    net_run_case("GET success", test_get_success);
    net_run_case("GET with no address", test_get_without_an_address);
    net_run_case("GET after a boot on saved values", test_get_after_boot_on_saved_values);
    net_run_case("GET unauthenticated", test_get_unauthenticated);
    net_run_case("GET unpermitted", test_get_unpermitted);
    net_run_case("PUT success", test_put_success);
    net_run_case("a write-only session may save", test_a_write_only_session_may_save);
    net_run_case("PUT unauthenticated", test_put_unauthenticated);
    net_run_case("the forgery checks", test_the_forgery_checks_come_with_the_session);
    net_run_case("PUT unpermitted", test_put_unpermitted);
    net_run_case("invalid values", test_invalid_values_get_field_errors);
    net_run_case("malformed bodies", test_malformed_bodies);
    net_run_case("content type and size", test_content_type_and_size);
    net_run_case("other methods", test_other_methods);
    net_run_case("one save at a time", test_one_save_at_a_time);
    net_run_case("a failed save", test_a_failed_save);
    net_run_case("the connection closes", test_the_connection_closes_after_every_put_answer);
    net_run_case("an ended session", test_an_ended_session_is_a_401);
    return net_finish("web_network_routes");
}
