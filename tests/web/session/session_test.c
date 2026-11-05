/* See session_test.h. */
#include "session_test.h"

#include "web_port.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

crux_access_session_t session_admin;
extern crux_access_session_t session_test_route_session;
extern unsigned session_test_route_calls;

static uint64_t web_clock_ms;
static bool random_fail;
static bool hash_fail;
static unsigned random_calls;
static uint8_t random_last[64];
static uint64_t random_state;

uint64_t crux_web_port_now_ms(void)
{
    return web_clock_ms;
}

void session_clock_advance(uint64_t delta_ms)
{
    web_clock_ms += delta_ms;
    access_fake_clock_advance(delta_ms);
}

/* Distinct bytes on every call, so that two tokens never agree by accident. Not random. */
bool crux_web_session_port_random(uint8_t *output, size_t length)
{
    ++random_calls;
    if (random_fail) {
        return false;
    }
    for (size_t index = 0; index < length; ++index) {
        random_state ^= random_state << 13;
        random_state ^= random_state >> 7;
        random_state ^= random_state << 17;
        output[index] = (uint8_t)(random_state >> 24);
    }
    memcpy(random_last, output, length < sizeof(random_last) ? length : sizeof(random_last));
    return true;
}

/* FNV-1a in four lanes. Deterministic and distinct enough for a table test, and not a real hash. */
bool crux_web_session_port_hash(const uint8_t *data, size_t length,
                                uint8_t digest[CRUX_WEB_SESSION_HASH_SIZE])
{
    if (hash_fail) {
        return false;
    }
    for (size_t lane = 0; lane < 4u; ++lane) {
        uint64_t value = 0xcbf29ce484222325u ^ (uint64_t)(lane + 1u);
        for (size_t index = 0; index < length; ++index) {
            value = (value ^ data[index]) * 0x100000001b3u;
        }
        memcpy(digest + lane * 8u, &value, 8u);
    }
    return true;
}

void session_fake_random_fail(bool fail)
{
    random_fail = fail;
}

unsigned session_fake_random_calls(void)
{
    return random_calls;
}

const uint8_t *session_fake_random_last(void)
{
    return random_last;
}

void session_fake_hash_fail(bool fail)
{
    hash_fail = fail;
}

static const char *const viewer_permissions[] = {"status.read"};
static const char *const administrator_permissions[] = {"status.read",
                                                        CRUX_ACCESS_PERMISSION_MANAGE};
static const crux_access_role_t session_roles[] = {
    {"administrator", administrator_permissions, 2},
    {"viewer", viewer_permissions, 1},
};
static const crux_access_policy_t session_policy = {session_roles, 2, 900u, 28800u, NULL};

void session_test_setup(void)
{
    static const crux_web_config_t config = {crux_web_session_authenticate, NULL, 5000u, 100u};
    static const crux_web_session_config_t session_config = {"https", NULL, 0u};
    uint32_t retry = 0;

    web_clock_ms = 1000u;
    session_test_route_session.value = 0u;
    session_test_route_calls = 0u;
    random_fail = false;
    hash_fail = false;
    random_calls = 0u;
    random_state = 0x9e3779b97f4a7c15u;
    access_fake_reset();
    crux_access_test_reset();
    CHECK(crux_access_init(&session_policy) == CRUX_ACCESS_OK);
    CHECK(crux_access_provision_first_account("admin", SESSION_TEST_PASSWORD,
                                              strlen(SESSION_TEST_PASSWORD), "administrator",
                                              &access_test_serial) == CRUX_ACCESS_OK);
    CHECK(crux_access_login_password("admin", SESSION_TEST_PASSWORD, strlen(SESSION_TEST_PASSWORD),
                                     &access_test_serial, &session_admin,
                                     &retry) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(session_admin, "viewer", "viewer", SESSION_TEST_PASSWORD,
                                     strlen(SESSION_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_create(session_admin, "forced", "viewer", SESSION_TEST_PASSWORD,
                                     strlen(SESSION_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    CHECK(crux_access_account_reset_password(session_admin, "forced", SESSION_TEST_PASSWORD,
                                             strlen(SESSION_TEST_PASSWORD)) == CRUX_ACCESS_OK);
    access_fake_events_clear();
    access_fake_derive_counters_clear();
    crux_web_core_init(&config);
    CHECK(crux_web_session_init(&session_config) == CRUX_WEB_OK);
    fake_transport_reset();
}

const fake_reply_t *session_send(const session_request_t *request)
{
    static fake_reply_t reply;
    static const uint8_t source[4] = {192, 0, 2, 9};
    crux_web_request_head_t head;
    char length[24];
    fake_client_t *client = fake_connect();
    size_t body_length = request->body == NULL ? 0u : strlen(request->body);

    memset(&reply, 0, sizeof(reply));
    CHECK(client != NULL);
    if (client == NULL) {
        return &reply;
    }
    memset(&head, 0, sizeof(head));
    head.cookie = request->cookie;
    head.origin = request->origin;
    head.host = request->host;
    head.xsrf_token = request->xsrf;
    head.crux_request = request->crux_request;
    head.content_type = request->content_type;
    memcpy(head.source, source, sizeof(source));
    head.source_length = sizeof(source);
    if (request->body != NULL) {
        snprintf(length, sizeof(length), "%zu", body_length);
        head.content_length = length;
    }
    if (fake_begin(client, request->method, request->url, head)) {
        fake_body(client, request->body, body_length, true);
    }
    reply = client->reply;
    if (client->open) {
        fake_disconnect(client);
    }
    return &reply;
}

size_t session_reply_headers(const fake_reply_t *reply, const char *name, const char **values,
                             size_t capacity)
{
    size_t found = 0;

    for (size_t index = 0; index < reply->header_count; ++index) {
        if (strcasecmp(reply->names[index], name) == 0) {
            if (found < capacity) {
                values[found] = reply->values[index];
            }
            ++found;
        }
    }
    return found;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool session_json_string(const char *body, const char *name, char *value, size_t size)
{
    char key[64];
    const char *start;
    const char *end;

    snprintf(key, sizeof(key), "\"%s\":\"", name);
    start = strstr(body, key);
    if (start == NULL) {
        return false;
    }
    start += strlen(key);
    end = strchr(start, '"');
    if (end == NULL || (size_t)(end - start) >= size) {
        return false;
    }
    memcpy(value, start, (size_t)(end - start));
    value[end - start] = '\0';
    return true;
}

const fake_reply_t *session_post_login(const char *user, const char *password, char *id,
                                       size_t size)
{
    char body[600];
    const fake_reply_t *reply;
    session_request_t request = {.method = CRUX_WEB_POST,
                                 .url = "/api/v1/session",
                                 .origin = SESSION_TEST_ORIGIN,
                                 .host = SESSION_TEST_HOST,
                                 .content_type = "application/json",
                                 .body = body};

    snprintf(body, sizeof(body), "{\"user\":\"%s\",\"password\":\"%s\"}", user, password);
    reply = session_send(&request);
    if (id != NULL && size != 0u) {
        id[0] = '\0';
        if (reply->status == 202) {
            CHECK(session_json_string(reply->body, "id", id, size));
        }
    }
    return reply;
}

const fake_reply_t *session_poll(const char *id)
{
    char url[160];
    session_request_t request = {
        .method = CRUX_WEB_GET, .url = url, .host = SESSION_TEST_HOST, .crux_request = "1"};

    snprintf(url, sizeof(url), "/api/v1/session/attempt/%s", id);
    return session_send(&request);
}

static bool cookie_value(const char *line, const char *name, char *value, size_t size)
{
    size_t name_length = strlen(name);
    size_t length;

    if (strncmp(line, name, name_length) != 0 || line[name_length] != '=') {
        return false;
    }
    line += name_length + 1u;
    length = strcspn(line, ";");
    if (length >= size) {
        return false;
    }
    memcpy(value, line, length);
    value[length] = '\0';
    return true;
}

const fake_reply_t *session_login(const char *user, const char *password, session_jar_t *jar)
{
    char id[80];
    const fake_reply_t *reply = session_post_login(user, password, id, sizeof(id));
    const char *cookies[4];
    size_t count;

    if (reply->status != 202) {
        return reply;
    }
    while (web_session_work_once()) {
    }
    reply = session_poll(id);
    if (jar != NULL) {
        memset(jar, 0, sizeof(*jar));
        count = session_reply_headers(reply, "Set-Cookie", cookies, 4u);
        for (size_t index = 0; index < count && index < 4u; ++index) {
            (void)cookie_value(cookies[index], "crux_session", jar->session, sizeof(jar->session));
            (void)cookie_value(cookies[index], "XSRF-TOKEN", jar->xsrf, sizeof(jar->xsrf));
        }
        snprintf(jar->header, sizeof(jar->header), "crux_session=%s; XSRF-TOKEN=%s", jar->session,
                 jar->xsrf);
    }
    return reply;
}

const fake_reply_t *session_with(const session_jar_t *jar, unsigned method, const char *url,
                                 const char *body)
{
    session_request_t request = {.method = method,
                                 .url = url,
                                 .cookie = jar->header,
                                 .host = SESSION_TEST_HOST,
                                 .body = body};

    if (method != CRUX_WEB_GET) {
        request.origin = SESSION_TEST_ORIGIN;
        request.xsrf = jar->xsrf;
        if (body != NULL) {
            request.content_type = "application/json";
        }
    }
    return session_send(&request);
}

int session_test_finish(const char *suite)
{
    return access_test_finish(suite);
}
