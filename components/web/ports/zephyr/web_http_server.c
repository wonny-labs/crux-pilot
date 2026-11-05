/* The private adapter over the Zephyr HTTP server: the only web source that names Zephyr.
 *
 * It holds no routing or access logic. Each generated resource calls the one callback here, which
 * hands the request to the portable core. Behaviour it depends on is from the server's source at
 * v4.4.2, and tests/native/web_http holds it to the real server:
 *
 * - A negative return from the callback closes the connection, unless it is -EAGAIN, which leaves
 * the client open with its resource held. Every close here therefore uses another error.
 * - A callback error before any response makes the server send a plain 500, so setting
 *   http1_headers_sent stops that when the adapter has answered or wants a silent close.
 * - A response that goes through the server is complete only when the body is complete, and its
 *   chunked terminator goes after the last callback. To answer before the body ends, the adapter
 *   writes a complete response itself and closes. It never does this on a kept connection.
 * - The server sends on a blocking socket with no timeout of its own, so a timeout is set on each
 *   request's first callback. It bounds one send, not a response: the server makes about twenty
 * sends for a small response, in one callback, without returning to poll.
 * - A send that fails with -EAGAIN comes back through the server, which swallows it and leaves the
 *   client open with its resource and this slot held. The response stops there, and the next
 * received byte runs the callback again, possibly long after another client's response has been
 * built in the same store. A callback that finds a finished exchange, or a piece with its
 * response's data_len or its store generation changed, is that re-entry, and it closes.
 * - The server calls back without returning to poll until a response is final, so a large body goes
 * in pieces, and a callback between pieces closes a response that has run over its deadline. A
 * response of one piece never reaches that check.
 */
#include "crux_web_zephyr_resources.h"

#include "crux_web_adapter.h"
#include "web_port.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/parser.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/posix/fnmatch.h>
#include <zephyr/sys/util.h>

/* A registered resource can be held by one client, so a slot per client is one per request. */
#define CRUX_WEB_SLOTS CONFIG_HTTP_SERVER_MAX_CLIENTS
#define CRUX_WEB_RESPONSE_HEADERS_MAX 16u
#define CRUX_WEB_RAW_HEAD_MAX 768u
/* Longer than any address form of Origin or Host that a device serves, with a name of 100 bytes. */
#define CRUX_WEB_ORIGIN_VALUE_MAX 128u
/* Cookie uses all of a parsed value. X-XSRF-TOKEN holds 256 bits in base64url. */
#define CRUX_WEB_CAPTURE_VALUES                                                                    \
    (CONFIG_HTTP_SERVER_MAX_HEADER_LEN + CRUX_WEB_ORIGIN_VALUE_MAX + CRUX_WEB_ORIGIN_VALUE_MAX +   \
     64u + 2u + 64u + 21u)
#define CRUX_WEB_CAPTURE_NAMES                                                                     \
    (sizeof("Cookie") + sizeof("Origin") + sizeof("Host") + sizeof("X-XSRF-TOKEN") +               \
     sizeof("X-Crux-Request") + sizeof("Content-Type") + sizeof("Content-Length"))

BUILD_ASSERT(IS_ENABLED(CONFIG_HTTP_SERVER_VERSION_1) && !IS_ENABLED(CONFIG_HTTP_SERVER_VERSION_2),
             "the web adapter handles HTTP/1.1 only");
BUILD_ASSERT(!IS_ENABLED(CONFIG_HTTP_SERVER_WEBSOCKET), "the web adapter has no WebSocket route");
BUILD_ASSERT(!IS_ENABLED(CONFIG_HTTP_SERVER_REPORT_FAILURE_REASON),
             "an error must not carry native error text");
/* The server passes Zephyr's FNM_* values to fnmatch(). The host's C library gives those numbers
 * other meanings, including no FNM_PATHNAME and a case-insensitive match, so a test there shows
 * nothing. */
BUILD_ASSERT(!IS_ENABLED(CONFIG_EXTERNAL_LIBC),
             "the server's wildcard pass needs Zephyr's fnmatch");
BUILD_ASSERT(CONFIG_HTTP_SERVER_CAPTURE_HEADER_COUNT >= 7, "the adapter captures seven headers");
BUILD_ASSERT(CONFIG_HTTP_SERVER_MAX_HEADER_LEN >= 256,
             "a parsed header value must hold both cookies");
BUILD_ASSERT(CONFIG_HTTP_SERVER_CAPTURE_HEADER_BUFFER_SIZE >
                 CRUX_WEB_CAPTURE_NAMES + CRUX_WEB_CAPTURE_VALUES,
             "the capture buffer must hold every captured header at its largest");

HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_cookie, "Cookie");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_origin, "Origin");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_host, "Host");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_xsrf, "X-XSRF-TOKEN");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_request, "X-Crux-Request");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_type, "Content-Type");
HTTP_SERVER_REGISTER_HEADER_CAPTURE(crux_web_capture_length, "Content-Length");

static uint16_t crux_web_port = CONFIG_CRUX_WEB_PORT;

/* The service has no fallback resource: the server does not release a fallback's holder when a
 * client disconnects. A path with no resource therefore gets the server's plain 404. */
#if defined(CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS)
HTTP_SERVICE_DEFINE(crux_web_service, NULL, &crux_web_port, CONFIG_HTTP_SERVER_MAX_CLIENTS,
                    CONFIG_CRUX_WEB_BACKLOG, NULL, NULL, NULL);
#else
static const sec_tag_t crux_web_sec_tags[] = {CONFIG_CRUX_WEB_TLS_SEC_TAG};

HTTPS_SERVICE_DEFINE(crux_web_service, NULL, &crux_web_port, CONFIG_HTTP_SERVER_MAX_CLIENTS,
                     CONFIG_CRUX_WEB_BACKLOG, NULL, NULL, NULL, crux_web_sec_tags,
                     sizeof(crux_web_sec_tags));
#endif

static struct {
    const struct http_client_ctx *client;
    crux_web_exchange_t *exchange;
    /* What is left of a response going out in pieces. */
    const uint8_t *rest;
    size_t rest_length;
    int64_t response_started_ms;
    /* What stays the same between pieces inside the server's loop, to tell a re-entry from it. */
    size_t response_data_len;
    uint32_t response_generation;
} crux_web_slots[CRUX_WEB_SLOTS];

/* Used only on the server thread, for a response that is sent before the next callback. */
static struct http_header crux_web_response_headers[CRUX_WEB_RESPONSE_HEADERS_MAX];
static char crux_web_raw_head[CRUX_WEB_RAW_HEAD_MAX];

uint64_t crux_web_port_now_ms(void)
{
    return (uint64_t)k_uptime_get();
}

static unsigned method_bit(enum http_method method)
{
    switch (method) {
    case HTTP_GET:
        return CRUX_WEB_GET;
    case HTTP_POST:
        return CRUX_WEB_POST;
    case HTTP_PUT:
        return CRUX_WEB_PUT;
    case HTTP_DELETE:
        return CRUX_WEB_DELETE;
    case HTTP_PATCH:
        return CRUX_WEB_PATCH;
    default:
        return 0u;
    }
}

static void capture(const char **field, const char *value, crux_web_request_head_t *head)
{
    if (*field != NULL) {
        head->duplicate_header = true;
    }
    *field = value;
}

static void read_head(struct http_client_ctx *client, const struct http_request_ctx *request,
                      crux_web_request_head_t *head)
{
    memset(head, 0, sizeof(*head));
    head->method = method_bit(client->method);
    head->url = (const char *)client->url_buffer;
    head->chunked = (client->parser.flags & F_CHUNKED) != 0u;
    head->upgrade = client->parser.upgrade != 0u || client->has_upgrade_header;
    /* A status other than OK is never read as "the client sent nothing". */
    head->headers_incomplete = request->headers_status != HTTP_HEADER_STATUS_OK;
    for (size_t index = 0; index < request->header_count; ++index) {
        const char *name = request->headers[index].name;
        const char *value = request->headers[index].value;

        if (strcmp(name, "Cookie") == 0) {
            capture(&head->cookie, value, head);
        } else if (strcmp(name, "Origin") == 0) {
            capture(&head->origin, value, head);
        } else if (strcmp(name, "Host") == 0) {
            capture(&head->host, value, head);
        } else if (strcmp(name, "X-XSRF-TOKEN") == 0) {
            capture(&head->xsrf_token, value, head);
        } else if (strcmp(name, "X-Crux-Request") == 0) {
            capture(&head->crux_request, value, head);
        } else if (strcmp(name, "Content-Type") == 0) {
            capture(&head->content_type, value, head);
        } else if (strcmp(name, "Content-Length") == 0) {
            capture(&head->content_length, value, head);
        }
    }
}

static int find_slot(const struct http_client_ctx *client)
{
    for (size_t index = 0; index < CRUX_WEB_SLOTS; ++index) {
        if (crux_web_slots[index].client == client) {
            return (int)index;
        }
    }
    return -1;
}

static void release_slot(const struct http_client_ctx *client, bool aborted)
{
    int slot = find_slot(client);

    if (slot >= 0) {
        crux_web_exchange_close(crux_web_slots[slot].exchange, aborted);
        memset(&crux_web_slots[slot], 0, sizeof(crux_web_slots[slot]));
    }
}

static int send_all(int fd, const void *data, size_t length)
{
    const char *cursor = data;

    while (length != 0u) {
        ssize_t sent = zsock_send(fd, cursor, length, 0);

        if (sent < 0) {
            return -errno;
        }
        cursor += sent;
        length -= (size_t)sent;
    }
    return 0;
}

/* Answer before the body has ended, then close. Best effort: the client may not be reading. */
static void send_and_close_response(struct http_client_ctx *client,
                                    const crux_web_response_t *response)
{
    size_t used = 0u;
    int written = snprintf(crux_web_raw_head, sizeof(crux_web_raw_head),
                           "HTTP/1.1 %d \r\nConnection: close\r\nContent-Length: %zu\r\n",
                           response->status, response->body_length);

    if (written > 0) {
        used = (size_t)written;
    }
    for (size_t index = 0; index < response->header_count; ++index) {
        written = snprintf(crux_web_raw_head + used, sizeof(crux_web_raw_head) - used, "%s: %s\r\n",
                           response->headers[index].name, response->headers[index].value);
        if (written < 0 || (size_t)written >= sizeof(crux_web_raw_head) - used) {
            return;
        }
        used += (size_t)written;
    }
    if (used + 2u >= sizeof(crux_web_raw_head)) {
        return;
    }
    memcpy(crux_web_raw_head + used, "\r\n", 2u);
    used += 2u;
    if (send_all(client->fd, crux_web_raw_head, used) == 0 && response->body_length != 0u) {
        (void)send_all(client->fd, response->body, response->body_length);
    }
}

/* Give the server the next piece of a body. The last piece makes the response final. */
static void next_piece(int slot, const uint8_t *body, size_t length,
                       struct http_response_ctx *server_response)
{
    size_t piece = MIN(length, CONFIG_CRUX_WEB_RESPONSE_PIECE);

    server_response->body = body;
    server_response->body_len = piece;
    server_response->final_chunk = piece == length;
    crux_web_slots[slot].rest = body + piece;
    crux_web_slots[slot].rest_length = length - piece;
}

static int apply_step(struct http_client_ctx *client, int slot, crux_web_step_t step,
                      const crux_web_response_t *response,
                      struct http_response_ctx *server_response)
{
    switch (step) {
    case CRUX_WEB_STEP_MORE:
        return 0;
    case CRUX_WEB_STEP_RESPOND:
        if (response->header_count > ARRAY_SIZE(crux_web_response_headers)) {
            break;
        }
        crux_web_slots[slot].response_started_ms = k_uptime_get();
        crux_web_slots[slot].response_data_len = client->data_len;
        crux_web_slots[slot].response_generation = crux_web_response_generation();
        for (size_t index = 0; index < response->header_count; ++index) {
            crux_web_response_headers[index].name = response->headers[index].name;
            crux_web_response_headers[index].value = response->headers[index].value;
        }
        server_response->status = (enum http_status)response->status;
        server_response->headers = crux_web_response_headers;
        server_response->header_count = response->header_count;
        next_piece(slot, response->body, response->body_length, server_response);
        return 0;
    case CRUX_WEB_STEP_RESPOND_CLOSE:
        send_and_close_response(client, response);
        break;
    case CRUX_WEB_STEP_CLOSE:
        break;
    }
    /* Whatever happens next, the server must not add its plain 500, and -EAGAIN would not close. */
    client->http1_headers_sent = true;
    return -ECONNABORTED;
}

static int begin_request(struct http_client_ctx *client, const struct http_request_ctx *request,
                         crux_web_step_t *step, crux_web_response_t *response, int *slot)
{
    static const struct zsock_timeval send_timeout = {
        .tv_sec = CONFIG_CRUX_WEB_SEND_TIMEOUT_MS / 1000,
        .tv_usec = (CONFIG_CRUX_WEB_SEND_TIMEOUT_MS % 1000) * 1000,
    };
    crux_web_request_head_t head;
    crux_web_exchange_t *exchange;

    if (zsock_setsockopt(client->fd, ZSOCK_SOL_SOCKET, ZSOCK_SO_SNDTIMEO, &send_timeout,
                         sizeof(send_timeout)) < 0) {
        return -EIO;
    }
    *slot = find_slot(NULL);
    exchange = *slot < 0 ? NULL : crux_web_exchange_open();
    if (exchange == NULL) {
        return -ENOMEM;
    }
    crux_web_slots[*slot].client = client;
    crux_web_slots[*slot].exchange = exchange;
    read_head(client, request, &head);
    *step = crux_web_exchange_start(exchange, &head, response);
    return 0;
}

int crux_web_zephyr_callback(struct http_client_ctx *client, enum http_transaction_status status,
                             const struct http_request_ctx *request,
                             struct http_response_ctx *server_response, void *user_data)
{
    crux_web_response_t response;
    crux_web_step_t step = CRUX_WEB_STEP_MORE;
    int slot;

    ARG_UNUSED(user_data);
    if (status == HTTP_SERVER_TRANSACTION_ABORTED || status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        release_slot(client, status == HTTP_SERVER_TRANSACTION_ABORTED);
        return 0;
    }

    slot = find_slot(client);
    if (slot < 0) {
        int result = begin_request(client, request, &step, &response, &slot);

        if (result < 0) {
            client->http1_headers_sent = true;
            return result;
        }
    } else if (crux_web_slots[slot].rest_length != 0u) {
        /* A send failed earlier and this client has sent something since. The rest may point into a
         * store that another client's response has overwritten, so nothing more of it may go out.
         */
        if (client->data_len != crux_web_slots[slot].response_data_len ||
            crux_web_response_generation() != crux_web_slots[slot].response_generation) {
            client->http1_headers_sent = true;
            return -ECONNABORTED;
        }
        if (k_uptime_get() - crux_web_slots[slot].response_started_ms >
            CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS) {
            crux_web_stats_count_response_deadline();
            client->http1_headers_sent = true;
            return -ECONNABORTED;
        }
        next_piece(slot, crux_web_slots[slot].rest, crux_web_slots[slot].rest_length,
                   server_response);
        return 0;
    } else if (crux_web_exchange_finished(crux_web_slots[slot].exchange)) {
        /* A response was produced and its send failed, or the client sent another request after it.
         */
        client->http1_headers_sent = true;
        return -ECONNABORTED;
    }
    if (step == CRUX_WEB_STEP_MORE) {
        /* On a method with no body the server's data is the query string. Only these carry a body.
         */
        bool has_body = (client->method == HTTP_POST || client->method == HTTP_PUT ||
                         client->method == HTTP_PATCH);

        step = crux_web_exchange_data(
            crux_web_slots[slot].exchange, has_body ? request->data : NULL,
            has_body ? request->data_len : 0u, status == HTTP_SERVER_REQUEST_DATA_FINAL, &response);
    }
    return apply_step(client, slot, step, &response, server_response);
}

/* What the server's wildcard pass needs of fnmatch(), which the build assertion cannot see of a C
 * library that brings its own: a star stops at a slash, a route takes deeper paths, and case
 * counts. */
static bool wildcard_pass_behaves(void)
{
    const int flags = FNM_PATHNAME | FNM_LEADING_DIR;

    return fnmatch("/a", "/a/b", flags) == 0 && fnmatch("/a/*/c", "/a/b/d/c", flags) != 0 &&
           fnmatch("/a", "/A", flags) != 0;
}

crux_web_result_t crux_web_start(const crux_web_config_t *config)
{
    if (config == NULL) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    if (!wildcard_pass_behaves() || !crux_web_routes_valid()) {
        return CRUX_WEB_ERR_FAILED;
    }
    memset(crux_web_slots, 0, sizeof(crux_web_slots));
    crux_web_core_init(config);
    return http_server_start() == 0 ? CRUX_WEB_OK : CRUX_WEB_ERR_FAILED;
}

crux_web_result_t crux_web_stop(void)
{
    return http_server_stop() == 0 ? CRUX_WEB_OK : CRUX_WEB_ERR_FAILED;
}
