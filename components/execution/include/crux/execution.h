#ifndef CRUX_EXECUTION_H
#define CRUX_EXECUTION_H

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
} crux_thread_t;
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_mutex_t;
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_semaphore_t;

typedef enum {
    CRUX_THREAD_PRIORITY_LOW = 0,
    CRUX_THREAD_PRIORITY_NORMAL = 1,
    CRUX_THREAD_PRIORITY_HIGH = 2
} crux_thread_priority_t;

typedef struct {
    /* Minimum usable bytes. Zero requests the provider default. */
    size_t stack_size_bytes;
    crux_thread_priority_t priority;
} crux_thread_options_t;

typedef void (*crux_thread_entry_t)(void *argument);

/* Thread context only. Waits: 0 polls; 1..INT32_MAX are monotonic relative ms.
 * No cancellation/ISR/forever support. See docs/execution-contract-proposal.md
 * and docs/execution-validation.md for the contract and provider limits.
 * Output storage must not hold a live resource. Failure invalidates create outputs.
 * Join timeout retains the lifetime; only successful join releases stack/argument ownership.
 * Callers serialise handle-variable writes and prohibit new admissions during destroy.
 */
crux_status_t crux_thread_create(const crux_thread_options_t *options, crux_thread_entry_t entry,
                                 void *argument, crux_thread_t *out);
crux_status_t crux_thread_join(crux_thread_t *thread, uint32_t timeout_ms);

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
