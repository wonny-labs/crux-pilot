/* The small example project of Issue #220 on the real web core: its page, its assets under the
 * strict policy, and its own route behind the session lookup. The route's source includes only
 * <crux/web.h>, which tests/build/test_project_sample.py checks. */
#include "web_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRICT_POLICY                                                                              \
    "default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; form-action "       \
    "'self'; frame-ancestors 'none'"
/* The suites' session lookup over real logins of the Access core: the viewer's cookie names a
 * signed-in session that holds no permission of the project's. */
#define SIGNED_IN web_viewer_cookie

static void the_page_and_its_assets_are_served_under_the_strict_policy(void)
{
    static const char *const paths[] = {"/", "/index.html", "/greeting.js", "/page.css"};
    fake_client_t *client = fake_connect();

    for (size_t index = 0; index < sizeof(paths) / sizeof(paths[0]); ++index) {
        const fake_reply_t *reply = fake_request(client, paths[index], CRUX_WEB_GET, NULL);

        CHECK(reply->origin == FAKE_FROM_CORE);
        CHECK(reply->status == 200);
        CHECK(strcmp(fake_reply_header(reply, "Content-Security-Policy"), STRICT_POLICY) == 0);
        CHECK(strcmp(fake_reply_header(reply, "Cache-Control"), "no-cache") == 0);
    }
}

static void the_route_needs_a_session_and_answers_json(void)
{
    fake_client_t *client = fake_connect();
    const fake_reply_t *reply =
        fake_request(client, "/api/v1/project/greeting", CRUX_WEB_GET, NULL);

    CHECK(reply->status == 401);
    reply = fake_request(client, "/api/v1/project/greeting", CRUX_WEB_GET, SIGNED_IN);
    CHECK(reply->status == 200);
    CHECK(strcmp(fake_reply_header(reply, "Content-Type"), "application/json") == 0);
    CHECK(strcmp(fake_reply_header(reply, "Content-Security-Policy"),
                 "default-src 'self'; frame-ancestors 'none'") == 0);
    CHECK(reply->body_length == strlen("{\"greeting\":\"Hello from the project route\"}"));
    CHECK(memcmp(reply->body, "{\"greeting\":\"Hello from the project route\"}",
                 reply->body_length) == 0);
    /* A method the route does not take, and a path beside it, get the server's own answers. */
    CHECK(fake_request(client, "/api/v1/project/greeting", CRUX_WEB_PUT, SIGNED_IN)->status == 405);
    CHECK(fake_request(client, "/api/v1/project/other", CRUX_WEB_GET, SIGNED_IN)->status == 404);
}

static void the_page_has_no_fallback(void)
{
    fake_client_t *client = fake_connect();

    CHECK(fake_request(client, "/greeting", CRUX_WEB_GET, NULL)->status == 404);
}

int main(void)
{
    web_test_setup(false);
    the_page_and_its_assets_are_served_under_the_strict_policy();
    the_route_needs_a_session_and_answers_json();
    the_page_has_no_fallback();
    return access_test_finish("web project sample");
}
