/**
 * @file
 * @brief Bounded, fixed-size copy queues.
 *
 * Thread context only. timeout_ms uses 0 for polling and 1..INT32_MAX for
 * relative monotonic milliseconds. No infinite waits or cancellation.
 * Callers prevent new operations during destruction and serialise handle writes.
 */
#ifndef CRUX_QUEUE_H
#define CRUX_QUEUE_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zero-invalid value handle. Do not construct or inspect reserved fields. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_queue_t;

/**
 * Create a queue for capacity records of exactly message_size bytes.
 * @param capacity Positive maximum number of records.
 * @param message_size Positive size of each copied record.
 * Output is written only on success. Check the status before using new storage;
 * a successful create must not overwrite an unrelated live queue.
 * @param[out] out Unchanged on failure.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_queue_create(size_t capacity, size_t message_size, crux_queue_t *out);

/**
 * Copy one record into the queue.
 * @param queue Valid queue handle.
 * @param record Caller-owned readable buffer, borrowed only for this call.
 * @param size Must equal the configured message size.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @return CRUX_OK, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT or another crux_status_t error.
 */
crux_status_t crux_queue_send(crux_queue_t queue, const void *record, size_t size,
                              uint32_t timeout_ms);

/**
 * Copy and consume the oldest record.
 * @param queue Valid queue handle.
 * @param[out] record Caller-owned writable buffer, unchanged on failure.
 * @param size Must equal the configured message size.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @return CRUX_OK, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT or another crux_status_t error.
 */
crux_status_t crux_queue_receive(crux_queue_t queue, void *record, size_t size,
                                 uint32_t timeout_ms);

/**
 * Destroy a queue with no admitted operations, discarding any queued records.
 * @param[in,out] queue Invalidated on success; retained if busy.
 * @return CRUX_OK, CRUX_ERR_BUSY or another crux_status_t error.
 */
crux_status_t crux_queue_destroy(crux_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif
