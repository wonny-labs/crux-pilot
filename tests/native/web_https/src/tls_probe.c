/*
 * Probes for the HTTPS measurements of Issue #185. Not for a product.
 *
 * - /probe/tls/stats: sends the server made and the TLS layer made beneath them, and the TLS
 * library's heap: Mbed TLS's own buffer, or the C library's arena that wolfSSL allocates from.
 * - /probe/tls/hold/{bytes}: takes that much from the TLS library's allocator and keeps it, so that
 * the next accept meets a full heap. 0 gives it all back.
 * - /probe/raw/split and /probe/raw/joined: one small response, with its five headers passed one by
 * one or in one value. They bypass the adapter, so the server's own writes are what is measured.
 * - /probe/raw/nodelay: sets TCP_NODELAY on this connection, then answers as split does.
 */
#include "web_probe.h"

#include <crux/web.h>

#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
#include <mbedtls/memory_buffer_alloc.h>
#include <mbedtls/platform.h>
#define TLS_ALLOC(bytes) mbedtls_calloc(1, (bytes))
#define TLS_FREE(pointer) mbedtls_free(pointer)
#define TLS_HEAP_SIZE CONFIG_MBEDTLS_HEAP_SIZE
#else
#define TLS_HEAP_SIZE 0
#define TLS_ALLOC(bytes) malloc(bytes)
#define TLS_FREE(pointer) free(pointer)
#endif

#define HELD_MAX 96

static void *held[HELD_MAX];
static size_t held_bytes;

static crux_web_result_t on_tls_stats(crux_web_request_t *request, crux_access_session_t session,
                                      void *context)
{
    char body[512];
    size_t used = 0u;
    size_t current = 0u;
    size_t largest = 0u;
    size_t blocks = 0u;
    struct sys_heap **heaps;
    int count = sys_heap_array_get(&heaps);

    (void)session;
    (void)context;
#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
    mbedtls_memory_buffer_alloc_cur_get(&current, &blocks);
    mbedtls_memory_buffer_alloc_max_get(&largest, &blocks);
#else
    (void)blocks;
#endif
    used += (size_t)snprintf(
        body, sizeof(body),
        "{\"outer_sends\":%u,\"inner_sends\":%u,\"real_eagain\":%u,\"mbed_heap\":%zu,"
        "\"mbed_heap_max\":%zu,\"mbed_heap_size\":%d,\"held\":%zu,\"heaps\":[",
        web_probe_outer_sends(), web_probe_inner_sends(), web_probe_real_eagain(), current, largest,
        TLS_HEAP_SIZE, held_bytes);
    for (int index = 0; index < count && used < sizeof(body); ++index) {
        struct sys_memory_stats stats;

        if (sys_heap_runtime_stats_get(heaps[index], &stats) != 0) {
            continue;
        }
        used += (size_t)snprintf(body + used, sizeof(body) - used,
                                 "%s{\"bytes\":%zu,\"used\":%zu,\"max\":%zu}", index ? "," : "",
                                 heaps[index]->init_bytes, stats.allocated_bytes,
                                 stats.max_allocated_bytes);
    }
    if (used + 3u > sizeof(body)) {
        return crux_web_respond_error(request, 503, "not_ready");
    }
    used += (size_t)snprintf(body + used, sizeof(body) - used, "]}");
    return crux_web_respond(request, 200, NULL, body, used);
}

static crux_web_result_t on_tls_hold(crux_web_request_t *request, crux_access_session_t session,
                                     void *context)
{
    char text[16];
    char body[64];
    unsigned long bytes;
    int length;

    (void)session;
    (void)context;
    if (crux_web_path_param(request, 0u, text, sizeof(text)) != CRUX_WEB_OK) {
        return crux_web_respond_error(request, 404, "not_found");
    }
    bytes = strtoul(text, NULL, 10);
    if (bytes == 0u) {
        for (size_t index = 0; index < HELD_MAX; ++index) {
            TLS_FREE(held[index]);
            held[index] = NULL;
        }
        held_bytes = 0u;
    } else {
        for (size_t index = 0; index < HELD_MAX; ++index) {
            if (held[index] == NULL) {
                held[index] = TLS_ALLOC(bytes);
                held_bytes += held[index] != NULL ? bytes : 0u;
                break;
            }
        }
    }
    length = snprintf(body, sizeof(body), "{\"held\":%zu}", held_bytes);
    return crux_web_respond(request, 200, NULL, body, (size_t)length);
}

static crux_web_result_t on_tls_reset_max(crux_web_request_t *request,
                                          crux_access_session_t session, void *context)
{
    struct sys_heap **heaps;
    int count = sys_heap_array_get(&heaps);

    (void)session;
    (void)context;
#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
    mbedtls_memory_buffer_alloc_max_reset();
#endif
    for (int index = 0; index < count; ++index) {
        (void)sys_heap_runtime_stats_reset_max(heaps[index]);
    }
    return crux_web_respond(request, 200, NULL, "{}", 2u);
}

CRUX_WEB_ROUTE(tls_stats, "/probe/tls/stats", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_tls_stats, NULL);
CRUX_WEB_ROUTE(tls_hold, "/probe/tls/hold/{bytes}", CRUX_WEB_GET, CRUX_WEB_PUBLIC, on_tls_hold,
               NULL);
CRUX_WEB_ROUTE(tls_reset_max, "/probe/tls/reset-max", CRUX_WEB_GET, CRUX_WEB_PUBLIC,
               on_tls_reset_max, NULL);

/* The same five headers that the adapter sends with a JSON response. */
static const struct http_header split_headers[] = {
    {"Content-Type", "application/json"},
    {"Content-Security-Policy", "default-src 'self'; frame-ancestors 'none'"},
    {"X-Content-Type-Options", "nosniff"},
    {"Referrer-Policy", "no-referrer"},
    {"Cache-Control", "no-store"},
};
/* The server writes a name, ": ", the value and a line end, so a value can carry the other lines.
 */
static const struct http_header joined_headers[] = {
    {"Content-Type", "application/json\r\n"
                     "Content-Security-Policy: default-src 'self'; frame-ancestors 'none'\r\n"
                     "X-Content-Type-Options: nosniff\r\n"
                     "Referrer-Policy: no-referrer\r\n"
                     "Cache-Control: no-store"},
};
static const char raw_body[] = "{\"ok\":true}";

static int answer(struct http_response_ctx *response, const struct http_header *headers,
                  size_t count)
{
    response->status = HTTP_200_OK;
    response->headers = headers;
    response->header_count = count;
    response->body = (const uint8_t *)raw_body;
    response->body_len = sizeof(raw_body) - 1u;
    response->final_chunk = true;
    return 0;
}

static int raw_callback(struct http_client_ctx *client, enum http_transaction_status status,
                        const struct http_request_ctx *request, struct http_response_ctx *response,
                        void *user_data)
{
    ARG_UNUSED(request);
    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }
    if (user_data == joined_headers) {
        return answer(response, joined_headers, ARRAY_SIZE(joined_headers));
    }
    if (user_data == NULL) {
        int one = 1;

        (void)zsock_setsockopt(client->fd, NET_IPPROTO_TCP, ZSOCK_TCP_NODELAY, &one, sizeof(one));
    }
    return answer(response, split_headers, ARRAY_SIZE(split_headers));
}

static struct http_resource_detail_dynamic raw_split_detail = {
    .common = {.bitmask_of_supported_http_methods = BIT(HTTP_GET),
               .type = HTTP_RESOURCE_TYPE_DYNAMIC},
    .cb = raw_callback,
    .user_data = (void *)split_headers,
};
static struct http_resource_detail_dynamic raw_joined_detail = {
    .common = {.bitmask_of_supported_http_methods = BIT(HTTP_GET),
               .type = HTTP_RESOURCE_TYPE_DYNAMIC},
    .cb = raw_callback,
    .user_data = (void *)joined_headers,
};
static struct http_resource_detail_dynamic raw_nodelay_detail = {
    .common = {.bitmask_of_supported_http_methods = BIT(HTTP_GET),
               .type = HTTP_RESOURCE_TYPE_DYNAMIC},
    .cb = raw_callback,
    .user_data = NULL,
};

HTTP_RESOURCE_DEFINE(probe_raw_split, crux_web_service, "/probe/raw/split", &raw_split_detail);
HTTP_RESOURCE_DEFINE(probe_raw_joined, crux_web_service, "/probe/raw/joined", &raw_joined_detail);
HTTP_RESOURCE_DEFINE(probe_raw_nodelay, crux_web_service, "/probe/raw/nodelay",
                     &raw_nodelay_detail);
