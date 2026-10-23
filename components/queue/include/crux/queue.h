#ifndef CRUX_QUEUE_H
#define CRUX_QUEUE_H
#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Reserved representation: zero is invalid. Do not construct/inspect handles. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_queue_t;
/* Thread context only. Exact-size copied records; buffers remain valid throughout
 * the call. Poll=0, finite=1..INT32_MAX monotonic ms. No cancellation/ISR/forever.
 * Failed receive preserves output. Owner prevents new admissions before destroy;
 * BUSY destroy preserves handle/resources. See docs/messaging-contract.md. */
crux_status_t crux_queue_create(size_t capacity, size_t message_size, crux_queue_t *out);
crux_status_t crux_queue_send(crux_queue_t queue, const void *record, size_t size,
                              uint32_t timeout_ms);
crux_status_t crux_queue_receive(crux_queue_t queue, void *record, size_t size,
                                 uint32_t timeout_ms);
crux_status_t crux_queue_destroy(crux_queue_t *queue);
#ifdef __cplusplus
}
#endif
#endif
