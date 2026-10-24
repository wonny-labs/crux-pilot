#ifndef CRUX_SYNC_H
#define CRUX_SYNC_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reserved value representation. Zero is invalid; do not construct or inspect handles. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_mutex_t;
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_semaphore_t;

/* Thread context only. Waits: 0 polls; 1..INT32_MAX are monotonic relative ms.
 * No cancellation/ISR/forever support. See docs/execution-contract-proposal.md
 * and docs/execution-validation.md for the contract and provider limits.
 * Output storage must not hold a live resource. Failure invalidates create outputs.
 * Callers serialise handle-variable writes and prohibit new admissions during destroy.
 */
crux_status_t crux_mutex_create(crux_mutex_t *out);
crux_status_t crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t crux_mutex_unlock(crux_mutex_t mutex);
crux_status_t crux_mutex_destroy(crux_mutex_t *mutex);

crux_status_t crux_semaphore_create(uint32_t initial, uint32_t maximum, crux_semaphore_t *out);
crux_status_t crux_semaphore_take(crux_semaphore_t semaphore, uint32_t timeout_ms);
crux_status_t crux_semaphore_give(crux_semaphore_t semaphore);
crux_status_t crux_semaphore_destroy(crux_semaphore_t *semaphore);

#ifdef __cplusplus
}
#endif

#endif
