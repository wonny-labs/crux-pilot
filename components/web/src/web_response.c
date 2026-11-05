/* Responses. One store serves every exchange, because a transport sends each response before it
 * calls the core again, on the one server thread. */
#include "web_internal.h"

#include <stdio.h>
#include <string.h>

#define CONTENT_TYPE_JSON "application/json"
#define CONTENT_TYPE_MAX 64u
#define ERROR_CODE_MAX 32u

static const char content_security_policy[] = "default-src 'self'; frame-ancestors 'none'";
static const char *const owned_names[] = {
    "Transfer-Encoding",       "Content-Length",         "Connection",      "Content-Type",
    "Content-Security-Policy", "X-Content-Type-Options", "Referrer-Policy", "Cache-Control",
};

static struct {
    crux_web_header_t headers[CRUX_WEB_FIXED_HEADERS + CRUX_WEB_EXTRA_HEADERS + 1u];
    char extra[CRUX_WEB_EXTRA_HEADER_BYTES];
    size_t extra_used;
    size_t extra_count;
    char content_type[CONTENT_TYPE_MAX];
    uint8_t body[CRUX_WEB_RESPONSE_MAX];
} store;

/* The extra headers sit after the fixed ones, so filling those in moves nothing. */
static void fill_response(crux_web_response_t *response, int status, const char *content_type,
                          const void *body, size_t length)
{
    store.headers[0] = (crux_web_header_t){"Content-Type", content_type};
    store.headers[1] = (crux_web_header_t){"Content-Security-Policy", content_security_policy};
    store.headers[2] = (crux_web_header_t){"X-Content-Type-Options", "nosniff"};
    store.headers[3] = (crux_web_header_t){"Referrer-Policy", "no-referrer"};
    store.headers[4] = (crux_web_header_t){"Cache-Control", "no-store"};
    response->status = status;
    response->headers = store.headers;
    response->header_count = CRUX_WEB_FIXED_HEADERS + store.extra_count;
    response->body = body;
    response->body_length = length;
}

void crux_web_response_begin(void)
{
    store.extra_used = 0u;
    store.extra_count = 0u;
}

void crux_web_error_response(crux_web_response_t *response, int status, const char *code)
{
    int length = snprintf((char *)store.body, sizeof(store.body), "{\"code\":\"%s\"}", code);

    if (status == 503) {
        store.headers[CRUX_WEB_FIXED_HEADERS + store.extra_count++] =
            (crux_web_header_t){"Retry-After", "1"};
    }
    fill_response(response, status, CONTENT_TYPE_JSON, store.body, (size_t)length);
}

/* HTTP names compare without regard to case, and the C library's function is not in ISO C. */
static char lower(char character)
{
    if (character < 'A' || character > 'Z') {
        return character;
    }
    return (char)(character + ('a' - 'A'));
}

static bool same_name(const char *first, const char *second)
{
    for (; *first != '\0' && *second != '\0'; ++first, ++second) {
        if (lower(*first) != lower(*second)) {
            return false;
        }
    }
    return *first == *second;
}

static bool valid_name(const char *name)
{
    if (name == NULL || *name == '\0') {
        return false;
    }
    for (; *name != '\0'; ++name) {
        char character = *name;

        if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') || character == '-')) {
            return false;
        }
    }
    return true;
}

/* Printable ASCII, so that no value can start another header. */
static bool valid_value(const char *value)
{
    if (value == NULL) {
        return false;
    }
    for (; *value != '\0'; ++value) {
        if (*value < 0x20 || *value > 0x7e) {
            return false;
        }
    }
    return true;
}

static bool can_respond(const crux_web_request_t *request, crux_web_result_t *result)
{
    if (request == NULL || request->response == NULL) {
        *result = CRUX_WEB_ERR_INVALID_ARGUMENT;
        return false;
    }
    if (request->responded) {
        *result = CRUX_WEB_ERR_ALREADY_RESPONDED;
        return false;
    }
    return true;
}

crux_web_result_t crux_web_add_header(crux_web_request_t *request, const char *name,
                                      const char *value)
{
    crux_web_result_t result = CRUX_WEB_OK;
    size_t name_size;
    size_t value_size;
    char *destination;

    if (!can_respond(request, &result)) {
        return result;
    }
    if (!valid_name(name) || !valid_value(value)) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < sizeof(owned_names) / sizeof(owned_names[0]); ++index) {
        if (same_name(name, owned_names[index])) {
            return CRUX_WEB_ERR_INVALID_ARGUMENT;
        }
    }
    name_size = strlen(name) + 1u;
    value_size = strlen(value) + 1u;
    if (store.extra_count == CRUX_WEB_EXTRA_HEADERS ||
        name_size + value_size > sizeof(store.extra) - store.extra_used) {
        return CRUX_WEB_ERR_TOO_LARGE;
    }
    destination = store.extra + store.extra_used;
    memcpy(destination, name, name_size);
    memcpy(destination + name_size, value, value_size);
    store.headers[CRUX_WEB_FIXED_HEADERS + store.extra_count++] =
        (crux_web_header_t){destination, destination + name_size};
    store.extra_used += name_size + value_size;
    return CRUX_WEB_OK;
}

crux_web_result_t crux_web_respond(crux_web_request_t *request, int status,
                                   const char *content_type, const void *body, size_t length)
{
    crux_web_result_t result = CRUX_WEB_OK;
    size_t type_size;

    if (!can_respond(request, &result)) {
        return result;
    }
    if (status < 200 || status > 599 || status == 204 || status == 304) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    if (length > sizeof(store.body)) {
        return CRUX_WEB_ERR_TOO_LARGE;
    }
    if (body == NULL && length != 0u) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    if (content_type == NULL) {
        content_type = CONTENT_TYPE_JSON;
    }
    if (!valid_value(content_type) || *content_type == '\0') {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    type_size = strlen(content_type) + 1u;
    if (type_size > sizeof(store.content_type)) {
        return CRUX_WEB_ERR_TOO_LARGE;
    }
    memcpy(store.content_type, content_type, type_size);
    if (length != 0u) {
        memcpy(store.body, body, length);
    }
    fill_response(request->response, status, store.content_type, store.body, length);
    request->responded = true;
    return CRUX_WEB_OK;
}

crux_web_result_t crux_web_respond_error(crux_web_request_t *request, int status, const char *code)
{
    crux_web_result_t result = CRUX_WEB_OK;
    size_t length;

    if (!can_respond(request, &result)) {
        return result;
    }
    if (status < 400 || status > 599 || code == NULL || *code == '\0') {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    length = strspn(code, "abcdefghijklmnopqrstuvwxyz_");
    if (code[length] != '\0' || length > ERROR_CODE_MAX) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    crux_web_error_response(request->response, status, code);
    request->responded = true;
    return CRUX_WEB_OK;
}

crux_web_result_t crux_web_respond_static(crux_web_request_t *request, int status,
                                          const crux_web_header_t *headers, size_t header_count,
                                          const void *body, size_t length)
{
    crux_web_result_t result = CRUX_WEB_OK;

    if (!can_respond(request, &result)) {
        return result;
    }
    if (status < 200 || status > 599 || status == 204 || status == 304 ||
        (body == NULL && length != 0u) || headers == NULL || header_count == 0u) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    request->response->status = status;
    request->response->headers = headers;
    request->response->header_count = header_count;
    request->response->body = body;
    request->response->body_length = length;
    request->responded = true;
    return CRUX_WEB_OK;
}
