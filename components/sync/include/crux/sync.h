/**
 * @file
 * @brief Portable mutexes and counting semaphores.
 *
 * Thread context only. timeout_ms waits use 0 for polling and 1..INT32_MAX
 * for relative monotonic milliseconds. No infinite waits or cancellation.
 * Callers prevent new operations during destruction and serialise handle writes.
 */
#ifndef CRUX_SYNC_H
#define CRUX_SYNC_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zero-invalid value handles. Do not construct or inspect reserved fields. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_mutex_t;
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_semaphore_t;

/**
 * Create a nonrecursive mutex.
 * Output is written only on success. Check the status before using new storage;
 * a successful create must not overwrite an unrelated live mutex.
 * @param[out] out Unchanged on failure.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_mutex_create(crux_mutex_t *out);

/**
 * Acquire a mutex without changing ownership on failure.
 * @param mutex Valid mutex handle.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @return CRUX_OK, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT or another crux_status_t error.
 */
crux_status_t crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);

/**
 * Release a mutex held by the calling thread.
 * @param mutex Valid mutex handle.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_mutex_unlock(crux_mutex_t mutex);

/**
 * Destroy an idle mutex.
 * @param[in,out] mutex Invalidated on success; retained if busy.
 * @return CRUX_OK, CRUX_ERR_BUSY or another crux_status_t error.
 */
crux_status_t crux_mutex_destroy(crux_mutex_t *mutex);

/**
 * Create a counting semaphore.
 * @param initial Initial token count, not greater than maximum.
 * @param maximum Positive maximum token count.
 * Output is written only on success. Check the status before using new storage;
 * a successful create must not overwrite an unrelated live semaphore.
 * @param[out] out Unchanged on failure.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_semaphore_create(uint32_t initial, uint32_t maximum, crux_semaphore_t *out);

/**
 * Consume one token when available.
 * @param semaphore Valid semaphore handle.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @return CRUX_OK, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT or another crux_status_t error.
 */
crux_status_t crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms);

/**
 * Add one token without exceeding the configured maximum.
 * @param semaphore Valid semaphore handle.
 * @return CRUX_ERR_BUSY if full; otherwise CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_semaphore_give(crux_semaphore_t semaphore);

/**
 * Destroy a semaphore with no admitted operations.
 * @param[in,out] semaphore Invalidated on success; retained if busy.
 * @return CRUX_OK, CRUX_ERR_BUSY or another crux_status_t error.
 */
crux_status_t crux_semaphore_destroy(crux_semaphore_t *semaphore);

#ifdef __cplusplus
}
#endif

#endif
