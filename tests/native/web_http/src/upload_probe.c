/* The flow control probe of Issue #184: can the single server thread hold back a large upload?
 *
 * The server reads whenever a socket is readable and calls back with what it read, and it offers no
 * way to pause one client. So the only back-pressure a callback has is to wait for room in a
 * bounded queue to a worker, and every other client waits with it. This is a bare resource on the
 * same service, because the public contract has no streaming route until Issue #188 chooses one.
 * The worker simulates storage: a chunk takes a little time, and every sixteenth a sector write's
 * stall.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>

#define CHUNK_BYTES 256u
#define QUEUE_CHUNKS 8u
#define CHUNK_MS 5
#define STALL_EVERY_CHUNKS 16u
#define STALL_MS 200
#define PUT_LIMIT_MS 2000

struct chunk {
    size_t length;
    uint8_t data[CHUNK_BYTES];
};

K_MSGQ_DEFINE(upload_queue, sizeof(struct chunk), QUEUE_CHUNKS, 4);

static struct k_thread worker_thread;
static K_THREAD_STACK_DEFINE(worker_stack, 2048);
static bool worker_started;
static volatile size_t worker_bytes;

static void upload_worker(void *first, void *second, void *third)
{
    struct chunk chunk;
    unsigned count = 0u;

    ARG_UNUSED(first);
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    while (true) {
        k_msgq_get(&upload_queue, &chunk, K_FOREVER);
        worker_bytes = worker_bytes + chunk.length;
        k_msleep(++count % STALL_EVERY_CHUNKS == 0u ? STALL_MS : CHUNK_MS);
    }
}

static struct {
    size_t received;
    int64_t blocked_total_ms;
    int64_t blocked_max_ms;
    bool overflow;
} upload_state;

static char upload_reply[160];
static const struct http_header upload_headers[] = {{"Content-Type", "application/json"}};

static void offer(const uint8_t *data, size_t length)
{
    while (length != 0u) {
        static struct chunk chunk;
        size_t piece = MIN(length, sizeof(chunk.data));
        int64_t started = k_uptime_get();
        int64_t blocked;

        chunk.length = piece;
        memcpy(chunk.data, data, piece);
        if (k_msgq_put(&upload_queue, &chunk, K_MSEC(PUT_LIMIT_MS)) != 0) {
            upload_state.overflow = true;
        }
        blocked = k_uptime_get() - started;
        upload_state.blocked_total_ms += blocked;
        upload_state.blocked_max_ms = MAX(upload_state.blocked_max_ms, blocked);
        data += piece;
        length -= piece;
    }
}

static int upload_callback(struct http_client_ctx *client, enum http_transaction_status status,
                           const struct http_request_ctx *request,
                           struct http_response_ctx *response, void *user_data)
{
    ARG_UNUSED(client);
    ARG_UNUSED(user_data);
    if (status == HTTP_SERVER_TRANSACTION_ABORTED || status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        memset(&upload_state, 0, sizeof(upload_state));
        return 0;
    }
    if (!worker_started) {
        worker_started = true;
        k_thread_create(&worker_thread, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
                        upload_worker, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
    }
    upload_state.received += request->data_len;
    offer(request->data, request->data_len);
    if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
        int length = snprintf(upload_reply, sizeof(upload_reply),
                              "{\"received\":%zu,\"blocked_total_ms\":%lld,\"blocked_max_ms\":%lld,"
                              "\"overflow\":%s}",
                              upload_state.received, (long long)upload_state.blocked_total_ms,
                              (long long)upload_state.blocked_max_ms,
                              upload_state.overflow ? "true" : "false");

        response->status = HTTP_200_OK;
        response->headers = upload_headers;
        response->header_count = ARRAY_SIZE(upload_headers);
        response->body = (const uint8_t *)upload_reply;
        response->body_len = (size_t)length;
        response->final_chunk = true;
    }
    return 0;
}

static struct http_resource_detail_dynamic upload_detail = {
    .common = {.bitmask_of_supported_http_methods = BIT(HTTP_POST),
               .type = HTTP_RESOURCE_TYPE_DYNAMIC},
    .cb = upload_callback,
};

HTTP_RESOURCE_DEFINE(probe_upload_resource, crux_web_service, "/probe/upload", &upload_detail);
