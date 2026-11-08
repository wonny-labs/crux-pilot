/* Routing: which resource and route a request reaches, with the server's order rules modelled. */
#include "web_test.h"

#include <string.h>

static fake_client_t *client;

static void fresh(void)
{
    web_test_setup(false);
    client = fake_connect();
    CHECK(client != NULL);
}

static void expect_json_404(const fake_reply_t *reply)
{
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == 404);
    CHECK(strcmp(reply->body, "{\"code\":\"not_found\"}") == 0);
    CHECK(web_record.calls == 0u);
}

/* Generated order: every wildcard before every fixed path, and deeper before shallower. */
static void test_generated_order(void)
{
    bool fixed_seen = false;
    size_t last_depth = 100u;

    for (size_t index = 0; index < fake_resource_spec_count; ++index) {
        const char *pattern = fake_resource_specs[index].pattern;
        bool wildcard = strchr(pattern, '*') != NULL;
        size_t depth = 0;

        for (const char *cursor = pattern; *cursor != '\0'; ++cursor) {
            depth += *cursor == '/' ? 1u : 0u;
        }
        if (wildcard) {
            CHECK(!fixed_seen);
            CHECK(depth <= last_depth);
            last_depth = depth;
        } else {
            if (!fixed_seen) {
                last_depth = 100u;
            }
            fixed_seen = true;
            CHECK(depth <= last_depth);
            last_depth = depth;
        }
    }
}

/* A path has one resource, so a second method does not get the server's 405. */
static void test_methods_merged(void)
{
    fresh();
    CHECK(strcmp(fake_resource_pattern(fake_resource_for("/api/v1/session")), "/api/v1/session") ==
          0);
    CHECK(fake_request(client, "/api/v1/session", CRUX_WEB_POST, NULL)->status == 200);
    CHECK(strcmp(web_record.route, "login") == 0);
    CHECK(fake_request(client, "/api/v1/session", CRUX_WEB_DELETE, web_viewer_cookie)->status ==
          200);
    CHECK(strcmp(web_record.route, "logout") == 0);
    for (size_t first = 0; first < fake_resource_spec_count; ++first) {
        for (size_t second = first + 1u; second < fake_resource_spec_count; ++second) {
            CHECK(strcmp(fake_resource_specs[first].pattern, fake_resource_specs[second].pattern) !=
                  0);
        }
    }
}

/* The wildcard pass takes the first match, and a fixed resource matches deeper paths too. */
static void test_wildcard_not_captured_by_shorter_fixed_route(void)
{
    fresh();
    CHECK(strcmp(fake_resource_pattern(fake_resource_for("/api/v1/session/attempt/x")),
                 "/api/v1/session/attempt/*") == 0);
    CHECK(fake_request(client, "/api/v1/session/attempt/abc", CRUX_WEB_GET, NULL)->status == 200);
    CHECK(strcmp(web_record.route, "attempt") == 0);
    CHECK(strcmp(web_record.params[0], "abc") == 0);
}

static void test_deeper_wildcard_before_shallower(void)
{
    fresh();
    CHECK(strcmp(fake_resource_pattern(fake_resource_for("/api/v1/files/logs/a.txt")),
                 "/api/v1/files/*/*") == 0);
    CHECK(
        fake_request(client, "/api/v1/files/logs/a.txt", CRUX_WEB_GET, web_viewer_cookie)->status ==
        200);
    CHECK(strcmp(web_record.route, "file_item") == 0);
    CHECK(strcmp(web_record.params[0], "logs") == 0);
    CHECK(strcmp(web_record.params[1], "a.txt") == 0);
    CHECK(fake_request(client, "/api/v1/files/logs", CRUX_WEB_GET, web_viewer_cookie)->status ==
          200);
    CHECK(strcmp(web_record.route, "file_list") == 0);
}

/* FNM_LEADING_DIR lets every route match deeper paths, and the whole-path comparison answers them.
 */
static void test_deeper_path_is_json_404(void)
{
    static const char *const deeper[] = {
        "/api/v1/status/x",     "/api/v1/status/x/y",  "/api/v1/session/attempt/x/y",
        "/api/v1/session/me/x", "/api/v1/files/a/b/c", "/api/v1/status/?a=1",
    };

    for (size_t index = 0; index < sizeof(deeper) / sizeof(deeper[0]); ++index) {
        fresh();
        expect_json_404(fake_request(client, deeper[index], CRUX_WEB_GET, web_admin_cookie));
    }
}

/* Comparison comes before access, so an unknown path says nothing about what a session may do. */
static void test_path_checked_before_access(void)
{
    fresh();
    web_auth_force = CRUX_WEB_AUTH_NO_SESSION;
    expect_json_404(fake_request(client, "/api/v1/status/x", CRUX_WEB_GET, NULL));
    CHECK(web_auth_calls == 0u);
}

static void test_parameter_rules(void)
{
    static const char *const refused[] = {
        "/api/v1/session/attempt/%2e%2e", "/api/v1/session/attempt/a%2Fb",
        "/api/v1/session/attempt/..",     "/api/v1/session/attempt/.",
        "/api/v1/session/attempt/a b",    "/api/v1/session/attempt/a;b",
    };

    for (size_t index = 0; index < sizeof(refused) / sizeof(refused[0]); ++index) {
        fresh();
        expect_json_404(fake_request(client, refused[index], CRUX_WEB_GET, NULL));
    }
    fresh();
    CHECK(fake_request(client, "/api/v1/session/attempt/A-z_0.9~", CRUX_WEB_GET, NULL)->status ==
          200);
    CHECK(strcmp(web_record.params[0], "A-z_0.9~") == 0);
    /* An empty parameter is not a route. */
    fresh();
    CHECK(fake_request(client, "/api/v1/session/attempt/", CRUX_WEB_GET, NULL)->status == 404);
    CHECK(web_record.calls == 0u);
}

static void test_query_is_not_part_of_the_path(void)
{
    fresh();
    CHECK(fake_request(client, "/api/v1/status?a=/b/c", CRUX_WEB_GET, web_viewer_cookie)->status ==
          200);
    CHECK(strcmp(web_record.route, "status") == 0);
    CHECK(strcmp(web_record.query, "a=/b/c") == 0);
    fresh();
    CHECK(fake_request(client, "/api/v1/session/attempt/abc?x=1", CRUX_WEB_GET, NULL)->status ==
          200);
    CHECK(strcmp(web_record.params[0], "abc") == 0);
    CHECK(strcmp(web_record.query, "x=1") == 0);
}

/* A fixed route wins over a parameter route of the same shape, in the exact pass and the core. */
static void test_fixed_route_wins(void)
{
    fresh();
    CHECK(fake_request(client, "/api/v1/config/save", CRUX_WEB_POST, web_admin_cookie)->status ==
          200);
    CHECK(strcmp(web_record.route, "config_save") == 0);
    fresh();
    CHECK(fake_request(client, "/api/v1/config/other", CRUX_WEB_GET, web_admin_cookie)->status ==
          200);
    CHECK(strcmp(web_record.route, "config_item") == 0);
    CHECK(strcmp(web_record.params[0], "other") == 0);
    /* Declared: the parameter value "save" is shadowed, and the server answers the method itself.
     */
    fresh();
    CHECK(fake_request(client, "/api/v1/config/save", CRUX_WEB_GET, web_admin_cookie)->status ==
          405);
    CHECK(client->reply.origin == FAKE_FROM_SERVER);
}

/* The server's own answers, declared in the design: plain text with no JSON or security headers. */
static void test_server_plain_responses(void)
{
    const fake_reply_t *reply;

    fresh();
    reply = fake_request(client, "/nothing/here", CRUX_WEB_GET, NULL);
    CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 404);
    CHECK(fake_reply_header(reply, "Content-Security-Policy") == NULL);
    reply = fake_request(client, "/api/v1/status", CRUX_WEB_PUT, web_admin_cookie);
    CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 405);
    CHECK(web_record.calls == 0u);
}

int main(void)
{
    test_generated_order();
    test_methods_merged();
    test_wildcard_not_captured_by_shorter_fixed_route();
    test_deeper_wildcard_before_shallower();
    test_deeper_path_is_json_404();
    test_path_checked_before_access();
    test_parameter_rules();
    test_query_is_not_part_of_the_path();
    test_fixed_route_wins();
    test_server_plain_responses();
    return access_test_finish("web routing");
}
