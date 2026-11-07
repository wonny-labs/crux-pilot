/* See network_web.h. */
#include "network_web.h"

#include <stdlib.h>

static session_jar_t jars[10];
static char jar_names[10][16];
static size_t jar_count;

static void end_direct_sessions(void)
{
    crux_access_session_t *all[] = {&s_admin, &s_reader,   &s_writer, &s_both,
                                    &s_saver, &s_stranger, &s_forced};

    for (size_t index = 0; index < sizeof(all) / sizeof(all[0]); ++index) {
        crux_access_logout(all[index]);
    }
}

void web_net_setup(void)
{
    jar_count = 0u;
    net_start();
    end_direct_sessions();
    session_test_setup_web();
    net_events_clear();
    access_fake_events_clear();
}

void web_net_setup_not_started(void)
{
    crux_network_boot_report_t report;

    jar_count = 0u;
    net_boot(&report);
    CHECK(crux_config_service_start(net_table(), NET_TABLE_ENTRIES) == CRUX_CONFIG_SERVICE_OK);
    end_direct_sessions();
    session_test_setup_web();
}

void web_net_setup_on_store(void)
{
    jar_count = 0u;
    net_start_on_store();
    end_direct_sessions();
    session_test_setup_web();
    net_events_clear();
    access_fake_events_clear();
}

const session_jar_t *web_net_jar(const char *user)
{
    for (size_t index = 0; index < jar_count; ++index) {
        if (strcmp(jar_names[index], user) == 0) {
            return &jars[index];
        }
    }
    CHECK(jar_count < 10u);
    snprintf(jar_names[jar_count], sizeof(jar_names[jar_count]), "%s", user);
    const char *login_user = strcmp(user, "admin") == 0 ? ACCESS_TEST_ADMIN : user;
    CHECK(session_login(login_user, SESSION_TEST_PASSWORD, &jars[jar_count])->status == 200);
    CHECK(jars[jar_count].session[0] != '\0');
    return &jars[jar_count++];
}

const fake_reply_t *web_net_get(const session_jar_t *jar)
{
    return session_with(jar, CRUX_WEB_GET, NETWORK_URL, NULL);
}

const fake_reply_t *web_net_put(const session_jar_t *jar, const char *body)
{
    return session_with(jar, CRUX_WEB_PUT, NETWORK_URL, body);
}

const fake_reply_t *web_net_anonymous(unsigned method, const char *body)
{
    session_request_t request = {
        .method = method, .url = NETWORK_URL, .host = SESSION_TEST_HOST, .body = body};

    if (method != CRUX_WEB_GET) {
        request.origin = SESSION_TEST_ORIGIN;
        request.content_type = body != NULL ? "application/json" : NULL;
    }
    return session_send(&request);
}

const fake_reply_t *web_net_vary(const session_jar_t *jar, unsigned method, const char *body,
                                 web_net_variation_t variation)
{
    session_request_t request = {.method = method,
                                 .url = NETWORK_URL,
                                 .cookie = jar->header,
                                 .host = SESSION_TEST_HOST,
                                 .body = body};

    if (method != CRUX_WEB_GET) {
        request.origin = variation.no_origin ? NULL : SESSION_TEST_ORIGIN;
        request.xsrf = variation.no_token ? NULL : jar->xsrf;
        if (body != NULL) {
            request.content_type = variation.content_type == NULL      ? "application/json"
                                   : variation.content_type[0] == '\0' ? NULL
                                                                       : variation.content_type;
        }
    }
    return session_send(&request);
}

void web_net_expect_body(const fake_reply_t *reply, int status, const char *body)
{
    CHECK(reply->origin == FAKE_FROM_CORE);
    if (strcmp(reply->body, body) != 0 || reply->status != status) {
        printf("  expected %d %s\n  got      %d %s\n", status, body, reply->status, reply->body);
    }
    CHECK(reply->status == status);
    CHECK(strcmp(reply->body, body) == 0);
}

void web_net_expect_error(const fake_reply_t *reply, int status, const char *code)
{
    char expected[64];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    web_net_expect_body(reply, status, expected);
}

void web_net_expect_headers(const fake_reply_t *reply, const char *content_type)
{
    const char *value = fake_reply_header(reply, "Content-Type");

    CHECK(value != NULL && strcmp(value, content_type) == 0);
    value = fake_reply_header(reply, "Cache-Control");
    CHECK(value != NULL && strcmp(value, "no-store") == 0);
    value = fake_reply_header(reply, "X-Content-Type-Options");
    CHECK(value != NULL && strcmp(value, "nosniff") == 0);
    value = fake_reply_header(reply, "Referrer-Policy");
    CHECK(value != NULL && strcmp(value, "no-referrer") == 0);
    value = fake_reply_header(reply, "Content-Security-Policy");
    CHECK(value != NULL && strstr(value, "default-src 'self'") != NULL);
    CHECK(fake_reply_header(reply, "Access-Control-Allow-Origin") == NULL);
}

void web_net_expect_retry_after(const fake_reply_t *reply)
{
    const char *value = fake_reply_header(reply, "Retry-After");

    CHECK(value != NULL && value[0] >= '1' && value[0] <= '9');
}
