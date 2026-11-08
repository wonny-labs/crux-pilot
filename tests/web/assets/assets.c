/* The packaged web assets served by the real web core through the fake transport: the headers, the
 * encoding, routing of / and of unknown paths, the policy, no session, and a client that goes away.
 * What the fake cannot show, a response sent in pieces, is in tests/native/web_assets. */
#include "web_assets_internal.h"

#include "crux_web_adapter.h"
#include "fake_transport.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition);          \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

static uint64_t clock_ms = 1000u;
static unsigned authenticate_calls;

uint64_t crux_web_port_now_ms(void)
{
    return clock_ms;
}

static crux_web_auth_t refuse_everyone(const crux_web_request_head_t *head,
                                       crux_access_session_t *session, void *context)
{
    (void)head;
    (void)session;
    (void)context;
    ++authenticate_calls;
    return CRUX_WEB_AUTH_NO_SESSION;
}

static void setup(void)
{
    crux_web_config_t config = {refuse_everyone, NULL, 5000u, 100u};

    authenticate_calls = 0u;
    crux_web_core_init(&config);
    fake_transport_reset();
}

#if ASSETS_EXPECT_INLINE
#define EXPECT_POLICY                                                                              \
    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; base-uri 'self'; "   \
    "form-action 'self'; frame-ancestors 'none'"
#else
#define EXPECT_POLICY                                                                              \
    "default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; form-action "       \
    "'self'; frame-ancestors 'none'"
#endif

typedef struct {
    const char *path;
    const char *type;
    const char *cache;
    bool gzip;
} expected_t;

/* Written by hand, so that the packager and the test do not agree by sharing a table. */
static const expected_t expected[] = {
    {"/index.html", "text/html; charset=utf-8", "no-cache", true},
    {"/app-Ab12Cd34.js", "text/javascript; charset=utf-8", "max-age=31536000, immutable", true},
    {"/style.css", "text/css; charset=utf-8", "no-cache", true},
    {"/logo.png", "image/png", "no-cache", false},
};
#define EXPECTED_COUNT (sizeof(expected) / sizeof(expected[0]))

static const crux_web_asset_t *table_entry(const char *path)
{
    for (size_t index = 0; index < crux_web_asset_count; ++index) {
        if (strcmp(crux_web_asset_table[index].path, path) == 0) {
            return &crux_web_asset_table[index];
        }
    }
    return NULL;
}

static const fake_reply_t *get(fake_client_t *client, const char *url)
{
    return fake_request(client, url, CRUX_WEB_GET, NULL);
}

static bool has_header(const fake_reply_t *reply, const char *name)
{
    return fake_reply_header(reply, name) != NULL;
}

static void check_served(const fake_reply_t *reply, const crux_web_asset_t *asset)
{
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == 200);
    CHECK(reply->body_length == asset->length);
    CHECK(memcmp(reply->body, asset->data, asset->length) == 0);
}

static void every_asset_is_served_with_its_headers(void)
{
    fake_client_t *client = fake_connect();

    CHECK(crux_web_asset_count == EXPECTED_COUNT);
    for (size_t index = 0; index < EXPECTED_COUNT; ++index) {
        const crux_web_asset_t *asset = table_entry(expected[index].path);
        const fake_reply_t *reply;

        CHECK(asset != NULL);
        reply = get(client, expected[index].path);
        check_served(reply, asset);
        CHECK(strcmp(fake_reply_header(reply, "Content-Type"), expected[index].type) == 0);
        CHECK(strcmp(fake_reply_header(reply, "Cache-Control"), expected[index].cache) == 0);
        if (expected[index].gzip) {
            CHECK(strcmp(fake_reply_header(reply, "Content-Encoding"), "gzip") == 0);
            CHECK(asset->length > 2u && asset->data[0] == 0x1fu && asset->data[1] == 0x8bu);
        } else {
            CHECK(!has_header(reply, "Content-Encoding"));
        }
        CHECK(strcmp(fake_reply_header(reply, "Content-Security-Policy"), EXPECT_POLICY) == 0);
        CHECK(strcmp(fake_reply_header(reply, "X-Content-Type-Options"), "nosniff") == 0);
        CHECK(strcmp(fake_reply_header(reply, "Referrer-Policy"), "no-referrer") == 0);
        /* The server owns these two, and the cap leaves room for what it adds. */
        CHECK(!has_header(reply, "Content-Length"));
        CHECK(!has_header(reply, "Transfer-Encoding"));
        CHECK(!has_header(reply, "Set-Cookie"));
        CHECK(reply->header_count <= CRUX_WEB_RESPONSE_HEADERS_MAX);
        CHECK(!client->reply.closed);
    }
}

static void the_policy_keeps_scripts_and_frames_fixed(void)
{
    fake_client_t *client = fake_connect();
    const char *policy = fake_reply_header(get(client, "/index.html"), "Content-Security-Policy");

    CHECK(strstr(policy, "script-src 'self';") != NULL);
    CHECK(strstr(policy, "frame-ancestors 'none'") != NULL);
    CHECK(strstr(policy, "unsafe-eval") == NULL);
    CHECK(strstr(policy, "http") == NULL && strchr(policy, '*') == NULL);
#if ASSETS_EXPECT_INLINE
    CHECK(strstr(policy, "style-src 'self' 'unsafe-inline';") != NULL);
    /* Only styles are weakened. */
    CHECK(strstr(policy, "script-src 'self' 'unsafe-inline'") == NULL);
#else
    CHECK(strstr(policy, "unsafe-inline") == NULL);
#endif
}

static void root_serves_index_and_the_two_are_one_copy(void)
{
    fake_client_t *client = fake_connect();
    const crux_web_asset_t *index = table_entry("/index.html");
    const fake_reply_t *root = get(client, "/");

    check_served(root, index);
    CHECK(strcmp(fake_reply_header(root, "Content-Type"), "text/html; charset=utf-8") == 0);
    CHECK(strcmp(fake_reply_header(root, "Cache-Control"), "no-cache") == 0);
    check_served(get(client, "/index.html"), index);
    /* A query does not change what is served, and the path is compared without it. */
    check_served(get(client, "/?lang=en"), index);
    check_served(get(client, "/index.html?v=2"), index);
}

static void unknown_paths_get_the_servers_plain_answer_with_no_fallback(void)
{
    fake_client_t *client = fake_connect();
    static const char *const unknown[] = {"/settings",    "/network",   "/assets/missing.js",
                                          "/favicon.ico", "/index.htm", "/INDEX.HTML"};

    for (size_t index = 0; index < sizeof(unknown) / sizeof(unknown[0]); ++index) {
        const fake_reply_t *reply = get(client, unknown[index]);

        CHECK(reply->origin == FAKE_FROM_SERVER);
        CHECK(reply->status == 404);
        CHECK(!has_header(reply, "Content-Security-Policy"));
        CHECK(memcmp(reply->body, table_entry("/index.html")->data, 4) != 0);
    }
}

static void a_deeper_path_under_an_asset_is_not_the_asset(void)
{
    fake_client_t *client = fake_connect();
    const fake_reply_t *reply = get(client, "//");

    /* The wildcard pass reads the resource "/" as a leading directory of "//", so the core answers.
     */
    CHECK(reply->origin == FAKE_FROM_CORE && reply->status == 404);
    CHECK(reply->body_length < 64u);
    reply = get(client, "/index.html/extra");

    /* The server's wildcard pass reaches the asset's resource, and the core compares the whole
     * path. */
    CHECK(reply->origin == FAKE_FROM_CORE);
    CHECK(reply->status == 404);
    CHECK(reply->body_length < 64u);
    reply = get(client, "/app-Ab12Cd34.js/x");
    CHECK(reply->origin == FAKE_FROM_CORE && reply->status == 404);
}

static void only_get_is_served(void)
{
    static const unsigned methods[] = {CRUX_WEB_POST, CRUX_WEB_PUT, CRUX_WEB_DELETE,
                                       CRUX_WEB_PATCH};
    fake_client_t *client = fake_connect();

    for (size_t index = 0; index < sizeof(methods) / sizeof(methods[0]); ++index) {
        const fake_reply_t *reply = fake_request(client, "/index.html", methods[index], NULL);

        CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 405);
        reply = fake_request(client, "/", methods[index], NULL);
        CHECK(reply->origin == FAKE_FROM_SERVER && reply->status == 405);
    }
}

static void one_registered_resource_per_asset_and_the_route_beside_them(void)
{
    /* Four files, / and the project's own route. No wildcard, so hash routing needs no fallback. */
    CHECK(fake_resource_spec_count == EXPECTED_COUNT + 2u);
    for (size_t index = 0; index < fake_resource_spec_count; ++index) {
        CHECK(strchr(fake_resource_specs[index].pattern, '*') == NULL);
    }
    CHECK(fake_resource_for("/") >= 0);
    CHECK(fake_resource_for("/index.html") >= 0);
    CHECK(fake_resource_for("/index.html") != fake_resource_for("/"));
    for (size_t index = 0; index < EXPECTED_COUNT; ++index) {
        CHECK(fake_resource_for(expected[index].path) >= 0);
    }
}

static void assets_need_no_session_and_the_api_still_does(void)
{
    fake_client_t *client = fake_connect();
    const fake_reply_t *reply;

    for (size_t index = 0; index < EXPECTED_COUNT; ++index) {
        CHECK(get(client, expected[index].path)->status == 200);
    }
    CHECK(get(client, "/")->status == 200);
    CHECK(authenticate_calls == 0u);
    reply = get(client, "/api/v1/ping");
    CHECK(reply->origin == FAKE_FROM_CORE && reply->status == 401);
    CHECK(authenticate_calls == 1u);
}

static void a_client_that_goes_away_leaves_nothing_held(void)
{
    const crux_web_asset_t *script = table_entry("/app-Ab12Cd34.js");
    crux_web_stats_t before;
    crux_web_stats_t after;
    fake_client_t *first = fake_connect();
    fake_client_t *second;

    crux_web_stats_get(&before);
    check_served(get(first, "/app-Ab12Cd34.js"), script);
    fake_disconnect(first);
    CHECK(!fake_resource_held("/app-Ab12Cd34.js"));
    second = fake_connect();
    check_served(get(second, "/app-Ab12Cd34.js"), script);
    /* Going away with no request in progress, and before the first request. */
    fake_disconnect(second);
    second = fake_connect();
    fake_disconnect(second);
    second = fake_connect();
    check_served(get(second, "/app-Ab12Cd34.js"), script);
    crux_web_stats_get(&after);
    CHECK(after.requests == before.requests + 3u);
    CHECK(after.aborted == before.aborted);
    CHECK(after.response_deadlines == before.response_deadlines);
}

static void clients_in_turn_share_each_asset(void)
{
    fake_client_t *clients[3];

    for (size_t round = 0; round < 3u; ++round) {
        for (size_t index = 0; index < 3u; ++index) {
            clients[index] = fake_connect();
            CHECK(clients[index] != NULL);
        }
        for (size_t index = 0; index < 3u; ++index) {
            for (size_t asset = 0; asset < EXPECTED_COUNT; ++asset) {
                check_served(get(clients[index], expected[asset].path),
                             table_entry(expected[asset].path));
            }
        }
        for (size_t index = 0; index < 3u; ++index) {
            fake_disconnect(clients[index]);
        }
    }
}

int main(void)
{
    static void (*const cases[])(void) = {
        every_asset_is_served_with_its_headers,
        the_policy_keeps_scripts_and_frames_fixed,
        root_serves_index_and_the_two_are_one_copy,
        unknown_paths_get_the_servers_plain_answer_with_no_fallback,
        a_deeper_path_under_an_asset_is_not_the_asset,
        only_get_is_served,
        one_registered_resource_per_asset_and_the_route_beside_them,
        assets_need_no_session_and_the_api_still_does,
        a_client_that_goes_away_leaves_nothing_held,
        clients_in_turn_share_each_asset,
    };

    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        setup();
        cases[index]();
    }
    printf("web_assets: %zu cases passed\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
