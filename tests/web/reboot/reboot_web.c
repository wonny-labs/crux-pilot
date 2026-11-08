/* See reboot_web.h. */
#include "reboot_web.h"

#include <crux/web.h>

#include <stdlib.h>

static session_jar_t jars[10];
static char jar_names[10][16];
static size_t jar_count;

/* The handlers' calls to crux_web_respond go through this, so that the record of the steps holds
 * the answer between the event that precedes it and the delay that follows it. */
// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_web_result_t __real_crux_web_respond(crux_web_request_t *request, int status,
                                          const char *content_type, const void *body,
                                          size_t length);

crux_web_result_t __wrap_crux_web_respond(crux_web_request_t *request, int status,
                                          const char *content_type, const void *body, size_t length)
{
    reboot_trace_note(STEP_RESPOND, status);
    return __real_crux_web_respond(request, status, content_type, body, length);
}
// NOLINTEND(bugprone-reserved-identifier)

/* A lock of the service that does not come in time, for the answer `503` `busy`. The service's own
 * wait is the one length that fails, as in tests/network_settings/lock_faults.c. */
// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTEND(bugprone-reserved-identifier)

static uint32_t failing_wait;

crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (failing_wait != 0u && timeout_ms == failing_wait) {
        return CRUX_ERR_TIMEOUT;
    }
    return __real_crux_mutex_lock(mutex, timeout_ms);
}

void web_reboot_fail_lock(uint32_t wait_ms)
{
    failing_wait = wait_ms;
}

static void end_direct_sessions(void)
{
    crux_access_session_t *all[] = {&s_admin, &s_rebooter, &s_reader, &s_stranger, &s_forced};

    for (size_t index = 0; index < sizeof(all) / sizeof(all[0]); ++index) {
        crux_access_logout(all[index]);
    }
}

void web_reboot_setup(void)
{
    jar_count = 0u;
    reboot_start();
    end_direct_sessions();
    session_test_setup_web();
    access_fake_events_clear();
    reboot_trace_clear();
}

void web_reboot_setup_not_started(void)
{
    jar_count = 0u;
    reboot_boot();
    end_direct_sessions();
    session_test_setup_web();
}

const session_jar_t *web_reboot_jar(const char *user)
{
    for (size_t index = 0; index < jar_count; ++index) {
        if (strcmp(jar_names[index], user) == 0) {
            return &jars[index];
        }
    }
    CHECK(jar_count < 10u);
    snprintf(jar_names[jar_count], sizeof(jar_names[jar_count]), "%s", user);
    CHECK(session_login(user, SESSION_TEST_PASSWORD, &jars[jar_count])->status == 200);
    CHECK(jars[jar_count].session[0] != '\0');
    /* A login is answered through the same wrapper, and it is not a step of the service. */
    reboot_trace_clear();
    return &jars[jar_count++];
}

const fake_reply_t *web_reboot_get(const session_jar_t *jar)
{
    return session_with(jar, CRUX_WEB_GET, REBOOT_URL, NULL);
}

const fake_reply_t *web_reboot_post(const session_jar_t *jar)
{
    return session_with(jar, CRUX_WEB_POST, REBOOT_URL, NULL);
}

const fake_reply_t *web_reboot_anonymous(unsigned method)
{
    session_request_t request = {.method = method, .url = REBOOT_URL, .host = SESSION_TEST_HOST};

    if (method != CRUX_WEB_GET) {
        request.origin = SESSION_TEST_ORIGIN;
    }
    return session_send(&request);
}

const fake_reply_t *web_reboot_vary(const session_jar_t *jar, unsigned method,
                                    web_reboot_variation_t variation)
{
    session_request_t request = {.method = method,
                                 .url = REBOOT_URL,
                                 .cookie = jar->header,
                                 .host = SESSION_TEST_HOST,
                                 .body = variation.body};

    if (method != CRUX_WEB_GET) {
        request.origin = variation.no_origin ? NULL : SESSION_TEST_ORIGIN;
        request.xsrf = variation.no_token ? NULL : jar->xsrf;
        if (variation.body != NULL) {
            request.content_type = "application/json";
        }
    }
    return session_send(&request);
}

void web_reboot_expect_body(const fake_reply_t *reply, int status, const char *body)
{
    CHECK(reply->origin == FAKE_FROM_CORE);
    if (strcmp(reply->body, body) != 0 || reply->status != status) {
        printf("  expected %d %s\n  got      %d %s\n", status, body, reply->status, reply->body);
    }
    CHECK(reply->status == status);
    CHECK(strcmp(reply->body, body) == 0);
}

void web_reboot_expect_error(const fake_reply_t *reply, int status, const char *code)
{
    char expected[64];

    snprintf(expected, sizeof(expected), "{\"code\":\"%s\"}", code);
    web_reboot_expect_body(reply, status, expected);
}

void web_reboot_expect_headers(const fake_reply_t *reply, const char *content_type)
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

void web_reboot_expect_retry_after(const fake_reply_t *reply)
{
    const char *value = fake_reply_header(reply, "Retry-After");

    CHECK(value != NULL && value[0] >= '1' && value[0] <= '9');
}
