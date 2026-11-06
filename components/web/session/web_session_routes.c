/* The session routes of web/api/openapi.yaml. A composition names this source to crux_web_routes(),
 * and components/web/reviewed-routes.txt lists them for review.
 *
 * Login and a password change put a password in the request. The core clears its own copy of the
 * body, and these handlers clear theirs. The server's copy stays in its client buffer until that
 * buffer is overwritten, so both routes close the connection after every response: the server then
 * clears the whole client, with that buffer and the captured headers in it. */
#include "web_session_internal.h"

#include <stdio.h>
#include <string.h>

#define WEB_SESSION_BODY_MAX CRUX_WEB_BODY_BUFFER

/* Two Set-Cookie lines go in the core's extra header store, which holds four names and values. */
#define COOKIE_LINE_MAX 112u
_Static_assert(2u * (sizeof("Set-Cookie") + COOKIE_LINE_MAX) <= 384u,
               "both cookies fit the core's extra header store");

static const char cookie_session_set[] =
    "crux_session=%s; Secure; HttpOnly; SameSite=Strict; Path=/";
static const char cookie_xsrf_set[] = "XSRF-TOKEN=%s; Secure; SameSite=Strict; Path=/";
static const char cookie_session_clear[] =
    "crux_session=; Secure; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
static const char cookie_xsrf_clear[] = "XSRF-TOKEN=; Secure; SameSite=Strict; Path=/; Max-Age=0";

/* The bodies are read here, on the server thread only, and cleared after each request. */
static uint8_t request_body[WEB_SESSION_BODY_MAX];
static char field_one[WEB_SESSION_SECRET_SIZE];
static char field_two[WEB_SESSION_SECRET_SIZE];

static void clear_request(void)
{
    web_session_wipe(request_body, sizeof(request_body));
    web_session_wipe(field_one, sizeof(field_one));
    web_session_wipe(field_two, sizeof(field_two));
}

static char lower(char character)
{
    if (character < 'A' || character > 'Z') {
        return character;
    }
    return (char)(character + ('a' - 'A'));
}

/* application/json, in any case, alone or with parameters after a semicolon. */
static bool json_content(const char *value)
{
    static const char expected[] = "application/json";

    if (value == NULL) {
        return false;
    }
    for (size_t index = 0; index < sizeof(expected) - 1u; ++index) {
        if (lower(value[index]) != expected[index]) {
            return false;
        }
    }
    value += sizeof(expected) - 1u;
    while (*value == ' ' || *value == '\t') {
        ++value;
    }
    return *value == '\0' || *value == ';';
}

/* The whole body into request_body, then the two named strings into field_one and field_two. */
static bool read_fields(crux_web_request_t *request, web_json_field_t fields[2])
{
    size_t total = 0;
    size_t transferred = 0;

    fields[0].buffer = field_one;
    fields[0].size = sizeof(field_one);
    fields[1].buffer = field_two;
    fields[1].size = sizeof(field_two);
    if (!json_content(crux_web_content_type(request))) {
        return false;
    }
    do {
        if (crux_web_read_body(request, request_body + total, sizeof(request_body) - total,
                               &transferred) != CRUX_WEB_OK) {
            return false;
        }
        total += transferred;
    } while (transferred != 0u && total < sizeof(request_body));
    return web_json_parse_strings(request_body, total, fields, 2u);
}

static crux_web_result_t respond_text(crux_web_request_t *request, int status, const char *body)
{
    return crux_web_respond(request, status, NULL, body, strlen(body));
}

static crux_web_result_t respond_queued(crux_web_request_t *request,
                                        const char id[WEB_SESSION_TOKEN_SIZE])
{
    char body[sizeof("{\"id\":\"\"}") + CRUX_WEB_SESSION_TOKEN_TEXT];

    snprintf(body, sizeof(body), "{\"id\":\"%s\"}", id);
    return respond_text(request, 202, body);
}

static crux_access_origin_t web_origin(const crux_web_request_head_t *head)
{
    crux_access_origin_t origin = {CRUX_ACCESS_CHANNEL_WEB, {0}, 0u};

    if (head->source_length <= sizeof(origin.source)) {
        memcpy(origin.source, head->source, head->source_length);
        origin.source_length = head->source_length;
    }
    return origin;
}

/* Queues the attempt, clears the request, and answers 202 or 503. */
static crux_web_result_t queue(crux_web_request_t *request, const web_attempt_request_t *attempt)
{
    char id[WEB_SESSION_TOKEN_SIZE];
    web_queue_t queued = web_session_attempt_queue(attempt, id);

    clear_request();
    if (queued != WEB_QUEUE_OK) {
        return crux_web_respond_error(request, 503, "busy");
    }
    return respond_queued(request, id);
}

static crux_web_result_t on_login(crux_web_request_t *request, crux_access_session_t session,
                                  void *context)
{
    const crux_web_request_head_t *head = crux_web_request_head(request);
    web_json_field_t fields[2] = {{.name = "user"}, {.name = "password"}};
    web_attempt_request_t attempt;

    (void)session;
    (void)context;
    (void)crux_web_close_after_response(request);
    if (!web_session_origin_accepted(head)) {
        return crux_web_respond_error(request, 403, "origin_rejected");
    }
    if (!read_fields(request, fields)) {
        clear_request();
        return crux_web_respond_error(request, 400, "bad_request");
    }
    memset(&attempt, 0, sizeof(attempt));
    attempt.kind = WEB_ATTEMPT_LOGIN;
    /* A name outside the account rules is an ordinary attempt that ends as rejected. One that does
     * not fit, or holds a NUL, becomes the empty name, which crux_access refuses the same way. */
    attempt.user =
        fields[0].length < CRUX_ACCESS_NAME_SIZE && !fields[0].holds_nul ? field_one : "";
    attempt.secret = field_two;
    attempt.secret_length = fields[1].length;
    attempt.origin = web_origin(head);
    return queue(request, &attempt);
}

static crux_web_result_t on_change_password(crux_web_request_t *request,
                                            crux_access_session_t session, void *context)
{
    const crux_web_request_head_t *head = crux_web_request_head(request);
    web_json_field_t fields[2] = {{.name = "current_password"}, {.name = "new_password"}};
    web_attempt_request_t attempt;

    (void)context;
    (void)crux_web_close_after_response(request);
    if (!read_fields(request, fields)) {
        clear_request();
        return crux_web_respond_error(request, 400, "bad_request");
    }
    memset(&attempt, 0, sizeof(attempt));
    attempt.kind = WEB_ATTEMPT_CHANGE;
    attempt.secret = field_one;
    attempt.secret_length = fields[0].length;
    attempt.new_secret = field_two;
    attempt.new_secret_length = fields[1].length;
    attempt.origin = web_origin(head);
    attempt.session = session;
    return queue(request, &attempt);
}

/* X-Crux-Request is exactly "1". A captured prefix, such as "1" from "1\0x", is a non-browser's. */
static bool request_header(const char *value)
{
    return value != NULL && value[0] == '1' && value[1] == '\0';
}

static crux_web_result_t respond_retry_later(crux_web_request_t *request, uint32_t retry_after_ms)
{
    char seconds[12];
    uint32_t whole = retry_after_ms / 1000u + (retry_after_ms % 1000u != 0u ? 1u : 0u);

    snprintf(seconds, sizeof(seconds), "%lu", (unsigned long)(whole == 0u ? 1u : whole));
    (void)crux_web_add_header(request, "Retry-After", seconds);
    return crux_web_respond_error(request, 429, "retry_later");
}

/* Hands the session to the client: the table first, then the two cookies. */
static crux_web_result_t respond_login_success(crux_web_request_t *request,
                                               web_attempt_result_t *result)
{
    char session_line[COOKIE_LINE_MAX];
    char xsrf_line[COOKIE_LINE_MAX];
    crux_web_result_t answer;

    if (!web_session_table_insert(result->token_hash, result->csrf_hash, result->session)) {
        crux_access_logout(&result->session);
        return crux_web_respond_error(request, 503, "busy");
    }
    snprintf(session_line, sizeof(session_line), cookie_session_set, result->token);
    snprintf(xsrf_line, sizeof(xsrf_line), cookie_xsrf_set, result->csrf);
    if (crux_web_add_header(request, "Set-Cookie", session_line) != CRUX_WEB_OK ||
        crux_web_add_header(request, "Set-Cookie", xsrf_line) != CRUX_WEB_OK) {
        web_session_table_remove(result->session);
        crux_access_logout(&result->session);
        answer = crux_web_respond_error(request, 503, "busy");
    } else {
        web_session_count_session();
        answer = respond_text(request, 200, "{\"state\":\"success\"}");
    }
    web_session_wipe(session_line, sizeof(session_line));
    web_session_wipe(xsrf_line, sizeof(xsrf_line));
    return answer;
}

/* A login's rejection says nothing more: wrong password, unknown or disabled name, a name outside
 * the rules and an unusable credential store all look the same. */
static crux_web_result_t respond_login(crux_web_request_t *request, web_attempt_result_t *result)
{
    switch (result->result) {
    case CRUX_ACCESS_OK:
        return respond_login_success(request, result);
    case CRUX_ACCESS_ERR_RETRY_LATER:
        return respond_retry_later(request, result->retry_after_ms);
    case CRUX_ACCESS_ERR_BUSY:
    case CRUX_ACCESS_ERR_NO_RESOURCES:
        return crux_web_respond_error(request, 503, "busy");
    default:
        return respond_text(request, 200, "{\"state\":\"rejected\"}");
    }
}

static crux_web_result_t respond_change(crux_web_request_t *request,
                                        const web_attempt_result_t *result)
{
    switch (result->result) {
    case CRUX_ACCESS_OK:
        return respond_text(request, 200, "{\"state\":\"success\"}");
    case CRUX_ACCESS_ERR_DENIED:
        return respond_text(request, 200,
                            "{\"state\":\"rejected\",\"code\":\"invalid_field\",\"fields\":[{"
                            "\"name\":\"current_password\",\"code\":\"incorrect\"}]}");
    case CRUX_ACCESS_ERR_PASSWORD_REJECTED:
        return respond_text(request, 200,
                            "{\"state\":\"rejected\",\"code\":\"invalid_field\",\"fields\":[{"
                            "\"name\":\"new_password\",\"code\":\"rule_not_met\"}]}");
    case CRUX_ACCESS_ERR_STORE:
        return respond_text(request, 200, "{\"state\":\"rejected\",\"code\":\"storage_failed\"}");
    case CRUX_ACCESS_ERR_INVALID_ARGUMENT:
        return respond_text(request, 200, "{\"state\":\"rejected\",\"code\":\"bad_request\"}");
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return respond_text(request, 200, "{\"state\":\"rejected\",\"code\":\"unauthorized\"}");
    case CRUX_ACCESS_ERR_RETRY_LATER:
        return respond_retry_later(request, result->retry_after_ms);
    case CRUX_ACCESS_ERR_BUSY:
    case CRUX_ACCESS_ERR_NO_RESOURCES:
        return crux_web_respond_error(request, 503, "busy");
    default:
        return respond_text(request, 200, "{\"state\":\"rejected\",\"code\":\"not_ready\"}");
    }
}

static crux_web_result_t on_attempt(crux_web_request_t *request, crux_access_session_t session,
                                    void *context)
{
    char id[WEB_SESSION_TOKEN_SIZE + 1u];
    web_attempt_result_t result;
    crux_web_result_t answer;

    (void)session;
    (void)context;
    /* Before anything else, so that a navigation learns nothing and changes nothing. */
    if (!request_header(crux_web_request_head(request)->crux_request)) {
        return crux_web_respond_error(request, 403, "csrf_rejected");
    }
    if (crux_web_path_param(request, 0u, id, sizeof(id)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    switch (web_session_attempt_poll(id, &result)) {
    case WEB_POLL_PENDING:
        return respond_text(request, 200, "{\"state\":\"pending\"}");
    case WEB_POLL_DONE:
        answer = result.kind == WEB_ATTEMPT_LOGIN ? respond_login(request, &result)
                                                  : respond_change(request, &result);
        web_session_wipe(&result, sizeof(result));
        return answer;
    case WEB_POLL_NOT_FOUND:
    default:
        return crux_web_respond_error(request, 404, "not_found");
    }
}

static crux_web_result_t on_logout(crux_web_request_t *request, crux_access_session_t session,
                                   void *context)
{
    (void)context;
    web_session_table_remove(session);
    crux_access_logout(&session);
    (void)crux_web_add_header(request, "Set-Cookie", cookie_session_clear);
    (void)crux_web_add_header(request, "Set-Cookie", cookie_xsrf_clear);
    return crux_web_respond(request, 200, "text/plain", NULL, 0u);
}

/* Role names come from the product's policy, so they are escaped. User names cannot need it. */
static void json_escape(char *output, size_t size, const char *text)
{
    size_t used = 0;

    for (; *text != '\0' && used + 7u < size; ++text) {
        unsigned char character = (unsigned char)*text;

        if (character == '"' || character == '\\') {
            output[used++] = '\\';
            output[used++] = (char)character;
        } else if (character < 0x20u) {
            used += (size_t)snprintf(output + used, size - used, "\\u%04x", character);
        } else {
            output[used++] = (char)character;
        }
    }
    output[used] = '\0';
}

static crux_web_result_t on_me(crux_web_request_t *request, crux_access_session_t session,
                               void *context)
{
    char user[CRUX_ACCESS_NAME_SIZE];
    char role[CRUX_ACCESS_NAME_SIZE];
    char escaped[CRUX_ACCESS_NAME_SIZE * 6u + 1u];
    char body[64 + CRUX_ACCESS_NAME_SIZE + sizeof(escaped)];
    bool must_change = false;
    crux_access_result_t result;

    (void)context;
    result = crux_access_session_user(session, user, sizeof(user));
    if (result == CRUX_ACCESS_OK) {
        result = crux_access_session_role(session, role, sizeof(role));
    }
    if (result == CRUX_ACCESS_OK) {
        result = crux_access_session_must_change_password(session, &must_change);
    }
    if (result == CRUX_ACCESS_ERR_SESSION_INVALID) {
        web_session_table_remove(session);
        return crux_web_respond_error(request, 401, "unauthorized");
    }
    if (result != CRUX_ACCESS_OK) {
        return crux_web_respond_error(request, 503, "not_ready");
    }
    json_escape(escaped, sizeof(escaped), role);
    snprintf(body, sizeof(body), "{\"user\":\"%s\",\"role\":\"%s\",\"must_change_password\":%s}",
             user, escaped, must_change ? "true" : "false");
    return respond_text(request, 200, body);
}

CRUX_WEB_ROUTE_BODY(crux_session_login, "/api/v1/session", CRUX_WEB_POST, CRUX_WEB_PUBLIC, on_login,
                    NULL, WEB_SESSION_BODY_MAX);
CRUX_WEB_ROUTE(crux_session_logout, "/api/v1/session", CRUX_WEB_DELETE, CRUX_WEB_SESSION, on_logout,
               NULL);
CRUX_WEB_ROUTE(crux_session_attempt, "/api/v1/session/attempt/{id}", CRUX_WEB_GET, CRUX_WEB_PUBLIC,
               on_attempt, NULL);
CRUX_WEB_ROUTE(crux_session_me, "/api/v1/session/me", CRUX_WEB_GET, CRUX_WEB_SESSION, on_me, NULL);
CRUX_WEB_ROUTE_BODY(crux_session_password, "/api/v1/session/me/password", CRUX_WEB_POST,
                    CRUX_WEB_SESSION, on_change_password, NULL, WEB_SESSION_BODY_MAX);

#ifdef CRUX_WEB_SESSION_TESTING
bool web_session_routes_hold_bytes(const void *bytes, size_t length);

static bool holds(const void *memory, size_t size, const void *bytes, size_t length)
{
    for (size_t offset = 0; offset + length <= size; ++offset) {
        if (memcmp((const uint8_t *)memory + offset, bytes, length) == 0) {
            return true;
        }
    }
    return false;
}

/* The handlers' own buffers. The worker's copy is on its stack, which no test can see. */
bool web_session_routes_hold_bytes(const void *bytes, size_t length)
{
    return holds(request_body, sizeof(request_body), bytes, length) ||
           holds(field_one, sizeof(field_one), bytes, length) ||
           holds(field_two, sizeof(field_two), bytes, length);
}
#endif
