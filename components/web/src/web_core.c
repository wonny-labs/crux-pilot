/* The portable request flow: whole-path comparison, form checks, access enforcement, the body, the
 * whole-request deadline and the handler. One thread runs all of it. */
#include "web_internal.h"
#include "web_port.h"

#include <string.h>

struct crux_web_exchange {
    bool in_use;
    bool finished;
    bool body_expected;
    unsigned data_calls;
    uint64_t started_ms;
    size_t declared_length;
    crux_access_session_t session;
    crux_web_request_head_t head;
    crux_web_request_t request;
    uint8_t body[CRUX_WEB_BODY_BUFFER];
};

static crux_web_exchange_t web_exchanges[CRUX_WEB_EXCHANGE_MAX];
static crux_web_config_t web_config;
static crux_web_stats_t web_stats;

#ifdef CRUX_WEB_TESTING
const crux_web_route_t *crux_web_core_test_route;
bool web_core_holds_bytes(const void *bytes, size_t length);

bool web_core_holds_bytes(const void *bytes, size_t length)
{
    for (size_t index = 0; index < CRUX_WEB_EXCHANGE_MAX; ++index) {
        for (size_t offset = 0; offset + length <= CRUX_WEB_BODY_BUFFER; ++offset) {
            if (memcmp(web_exchanges[index].body + offset, bytes, length) == 0) {
                return true;
            }
        }
    }
    return false;
}
#endif

/* The body may hold a password, so this must not be optimised away. */
static void clear_body(crux_web_exchange_t *exchange)
{
    volatile uint8_t *cursor = exchange->body;

    for (size_t index = 0; index < sizeof(exchange->body); ++index) {
        cursor[index] = 0u;
    }
    exchange->request.body_length = 0u;
    exchange->request.body_read = 0u;
}

static crux_web_step_t reject(crux_web_exchange_t *exchange, crux_web_response_t *response,
                              int status, const char *code, bool may_keep_connection)
{
    ++web_stats.rejected;
    exchange->finished = true;
    clear_body(exchange);
    crux_web_response_begin();
    crux_web_error_response(response, status, code);
    return may_keep_connection && !exchange->body_expected ? CRUX_WEB_STEP_RESPOND
                                                           : CRUX_WEB_STEP_RESPOND_CLOSE;
}

/* Digits only, as HTTP defines it. Too large to hold reads as the largest value, which any limit
 * refuses. */
static bool parse_content_length(const char *text, size_t *length)
{
    size_t value = 0u;

    if (text == NULL) {
        *length = 0u;
        return true;
    }
    if (*text == '\0') {
        return false;
    }
    for (; *text != '\0'; ++text) {
        size_t digit;

        if (*text < '0' || *text > '9') {
            return false;
        }
        digit = (size_t)(*text - '0');
        value = value > (SIZE_MAX - digit) / 10u ? SIZE_MAX : value * 10u + digit;
    }
    *length = value;
    return true;
}

static bool is_session_class(const char *access)
{
    return strcmp(access, CRUX_WEB_SESSION) == 0;
}

/* The access class of a route, for the session that the lookup found. */
static crux_web_step_t check_class(crux_web_exchange_t *exchange, crux_web_response_t *response)
{
    const char *access = exchange->request.route->access;
    crux_access_result_t result;

    if (is_session_class(access)) {
        bool permitted;

        /* The answer does not matter here: this says only whether the session is valid, with no
         * event and no refreshed timer. A session under a forced change may use session routes. */
        result = crux_access_permitted(exchange->session, CRUX_ACCESS_PERMISSION_SELF, &permitted);
    } else {
        result = crux_access_check(exchange->session, access);
    }
    switch (result) {
    case CRUX_ACCESS_OK:
        return CRUX_WEB_STEP_MORE;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return reject(exchange, response, 401, "unauthorized", true);
    case CRUX_ACCESS_ERR_NOT_READY:
        return reject(exchange, response, 503, "not_ready", true);
    default:
        return reject(exchange, response, 403, "permission_denied", true);
    }
}

static crux_web_step_t enforce_access(crux_web_exchange_t *exchange, crux_web_response_t *response)
{
    const char *access = exchange->request.route->access;

    if (access == NULL || access[0] == '\0') {
        /* Only a route that did not come through the macro can be like this. */
        return reject(exchange, response, 403, "permission_denied", true);
    }
    if (strcmp(access, CRUX_WEB_PUBLIC) == 0) {
        return CRUX_WEB_STEP_MORE;
    }
    if (web_config.authenticate == NULL) {
        return reject(exchange, response, 503, "not_ready", true);
    }
    switch (web_config.authenticate(&exchange->head, &exchange->session,
                                    web_config.authenticate_context)) {
    case CRUX_WEB_AUTH_OK:
        return check_class(exchange, response);
    case CRUX_WEB_AUTH_NO_SESSION:
        return reject(exchange, response, 401, "unauthorized", true);
    case CRUX_WEB_AUTH_CSRF_REJECTED:
        return reject(exchange, response, 403, "csrf_rejected", true);
    case CRUX_WEB_AUTH_ORIGIN_REJECTED:
        return reject(exchange, response, 403, "origin_rejected", true);
    case CRUX_WEB_AUTH_NOT_READY:
    default:
        return reject(exchange, response, 503, "not_ready", true);
    }
}

static crux_web_step_t run_handler(crux_web_exchange_t *exchange, crux_web_response_t *response)
{
    crux_web_request_t *request = &exchange->request;
    const crux_web_route_t *route = request->route;
    uint64_t started;
    uint64_t elapsed;
    crux_web_result_t result;

    request->responded = false;
    request->response = response;
    memset(response, 0, sizeof(*response));
    crux_web_response_begin();
    started = crux_web_port_now_ms();
    ++web_stats.handlers;
    result = route->handler(request, exchange->session, route->context);
    elapsed = crux_web_port_now_ms() - started;
    if (elapsed > web_stats.handler_max_ms) {
        web_stats.handler_max_ms = elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
    }
    if (elapsed > web_config.handler_limit_ms) {
        ++web_stats.handler_overruns;
    }
    request->response = NULL;
    exchange->finished = true;
    clear_body(exchange);
    if (!request->responded) {
        /* The ErrorCode list has no code for a handler's own fault, and a new one is a change to
         * the API specification. Extra headers from before the failure go with it. */
        (void)result;
        crux_web_response_begin();
        crux_web_error_response(response, 503, "not_ready");
    }
    return CRUX_WEB_STEP_RESPOND;
}

void crux_web_core_init(const crux_web_config_t *config)
{
    memset(web_exchanges, 0, sizeof(web_exchanges));
    memset(&web_stats, 0, sizeof(web_stats));
    web_config = *config;
}

crux_web_exchange_t *crux_web_exchange_open(void)
{
    for (size_t index = 0; index < CRUX_WEB_EXCHANGE_MAX; ++index) {
        crux_web_exchange_t *exchange = &web_exchanges[index];

        if (!exchange->in_use) {
            exchange->in_use = true;
            exchange->finished = false;
            exchange->body_expected = false;
            exchange->data_calls = 0u;
            exchange->declared_length = 0u;
            exchange->session.value = 0u;
            memset(&exchange->request, 0, sizeof(exchange->request));
            return exchange;
        }
    }
    return NULL;
}

crux_web_step_t crux_web_exchange_start(crux_web_exchange_t *exchange,
                                        const crux_web_request_head_t *head,
                                        crux_web_response_t *response)
{
    crux_web_request_t *request = &exchange->request;
    size_t path_length;

    memset(response, 0, sizeof(*response));
    exchange->head = *head;
    exchange->started_ms = crux_web_port_now_ms();
    request->head = &exchange->head;
    request->body = exchange->body;
    ++web_stats.requests;

    if (head->upgrade) {
        /* The server has already sent its own 426, and would go on to run the resource. */
        ++web_stats.rejected;
        exchange->finished = true;
        return CRUX_WEB_STEP_CLOSE;
    }
    exchange->body_expected = head->chunked;
    if (head->headers_incomplete || head->duplicate_header || head->chunked ||
        !parse_content_length(head->content_length, &exchange->declared_length)) {
        /* A dropped header is never read as having no session. A chunked body reaches a callback
         * with its framing in it. */
        return reject(exchange, response, 400, "bad_request", false);
    }
    exchange->body_expected = exchange->declared_length != 0u;

    path_length = strcspn(head->url, "?#");
    request->path_length = path_length;
    request->route = crux_web_route_find(head->url, path_length, head->method, request);
    if (request->route == NULL) {
        return reject(exchange, response, 404, "not_found", true);
    }
    if (exchange->body_expected && (head->method & (CRUX_WEB_GET | CRUX_WEB_DELETE)) != 0u) {
        /* The server calls back once per read here, which would run a handler more than once. */
        return reject(exchange, response, 400, "bad_request", false);
    }
    if (exchange->declared_length > request->route->body_max) {
        return reject(exchange, response, 413, "payload_too_large", false);
    }
    return enforce_access(exchange, response);
}

crux_web_step_t crux_web_exchange_data(crux_web_exchange_t *exchange, const void *data,
                                       size_t length, bool last, crux_web_response_t *response)
{
    crux_web_request_t *request = &exchange->request;

    memset(response, 0, sizeof(*response));
    if (exchange->finished || request->route == NULL) {
        return CRUX_WEB_STEP_CLOSE;
    }
    if (crux_web_port_now_ms() - exchange->started_ms > web_config.request_deadline_ms) {
        ++web_stats.deadlines;
        exchange->finished = true;
        clear_body(exchange);
        return CRUX_WEB_STEP_CLOSE;
    }
    ++exchange->data_calls;
    if (length > exchange->declared_length - request->body_length) {
        /* The server passes its whole buffer, so these bytes may be the next request's. */
        return reject(exchange, response, 400, "bad_request", false);
    }
    if (length != 0u) {
        memcpy(exchange->body + request->body_length, data, length);
        request->body_length += length;
    }
    if (!last) {
        return CRUX_WEB_STEP_MORE;
    }
    if (request->body_length != exchange->declared_length) {
        return reject(exchange, response, 400, "bad_request", false);
    }
    exchange->body_expected = false;
    /* What a session may do can change while a long body arrives. */
    if (exchange->data_calls > 1u && strcmp(request->route->access, CRUX_WEB_PUBLIC) != 0) {
        crux_web_step_t step = check_class(exchange, response);

        if (step != CRUX_WEB_STEP_MORE) {
            return step;
        }
    }
    return run_handler(exchange, response);
}

bool crux_web_exchange_finished(const crux_web_exchange_t *exchange)
{
    return exchange->finished;
}

void crux_web_exchange_close(crux_web_exchange_t *exchange, bool aborted)
{
    if (aborted && !exchange->finished) {
        ++web_stats.aborted;
    }
    clear_body(exchange);
    exchange->in_use = false;
}

void crux_web_stats_get(crux_web_stats_t *stats)
{
    *stats = web_stats;
}

void crux_web_stats_count_response_deadline(void)
{
    ++web_stats.response_deadlines;
}

const crux_web_request_head_t *crux_web_request_head(const crux_web_request_t *request)
{
    return request->head;
}

unsigned crux_web_method(const crux_web_request_t *request)
{
    return request->head->method;
}

const char *crux_web_content_type(const crux_web_request_t *request)
{
    return request->head->content_type;
}

size_t crux_web_body_length(const crux_web_request_t *request)
{
    return request->body_length;
}

crux_web_result_t crux_web_read_body(crux_web_request_t *request, void *buffer, size_t length,
                                     size_t *transferred)
{
    size_t remaining;

    if (request == NULL || buffer == NULL || transferred == NULL) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    remaining = request->body_length - request->body_read;
    if (length > remaining) {
        length = remaining;
    }
    memcpy(buffer, request->body + request->body_read, length);
    request->body_read += length;
    *transferred = length;
    return CRUX_WEB_OK;
}

static crux_web_result_t copy_text(const char *text, size_t length, char *buffer, size_t size)
{
    if (buffer == NULL || size == 0u) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    if (length >= size) {
        buffer[0] = '\0';
        return CRUX_WEB_ERR_TOO_LARGE;
    }
    memcpy(buffer, text, length);
    buffer[length] = '\0';
    return CRUX_WEB_OK;
}

crux_web_result_t crux_web_path_param(const crux_web_request_t *request, size_t index, char *buffer,
                                      size_t size)
{
    if (request == NULL) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    if (index >= request->param_count) {
        return CRUX_WEB_ERR_NOT_FOUND;
    }
    return copy_text(request->head->url + request->params[index].offset,
                     request->params[index].length, buffer, size);
}

crux_web_result_t crux_web_query(const crux_web_request_t *request, char *buffer, size_t size)
{
    const char *query;

    if (request == NULL) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    query = request->head->url + request->path_length;
    if (*query == '?') {
        ++query;
        return copy_text(query, strcspn(query, "#"), buffer, size);
    }
    return copy_text("", 0u, buffer, size);
}
