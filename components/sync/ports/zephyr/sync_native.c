#include "interrupt.h"
#include "sync_internal.h"
#include <errno.h>
#include <limits.h>
#include <stdatomic.h>

/* Common code stores the mutex owner atomically; a lock behind it would be a hidden lock. */
BUILD_ASSERT(ATOMIC_POINTER_LOCK_FREE == 2, "Sync needs lock-free pointer atomics");

bool crux_sync_context_allowed(void)
{
    return !crux_is_in_isr();
}

crux_status_t crux_sync_make_deadline(uint32_t timeout_ms, sync_native_deadline_t *out)
{
    if (timeout_ms > INT32_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->poll = timeout_ms == 0;
    if (timeout_ms == 0) {
        out->timeout = K_NO_WAIT;
        return CRUX_OK;
    }
    int64_t now = k_uptime_ticks();
    int64_t duration = k_ms_to_ticks_ceil64(timeout_ms);
    /* One extra tick covers the unobserved fraction of the current tick. */
    if (now > INT64_MAX - duration - 1) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->timeout = K_TIMEOUT_ABS_TICKS(now + duration + 1);
    return CRUX_OK;
}

static crux_status_t native_wait_result(int result)
{
    if (result == 0) {
        return CRUX_OK;
    }
    if (result == -EBUSY) {
        return CRUX_ERR_BUSY;
    }
    if (result == -EAGAIN) {
        return CRUX_ERR_TIMEOUT;
    }
    return CRUX_ERR_IO;
}

sync_native_identity_t crux_sync_current_identity(void)
{
    return k_current_get();
}

bool crux_sync_identity_equal(sync_native_identity_t first, sync_native_identity_t second)
{
    return first == second;
}

crux_status_t crux_mutex_impl_create(sync_native_mutex_t *mutex)
{
    return k_mutex_init(mutex) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t crux_mutex_impl_lock(sync_native_mutex_t *mutex,
                                   const sync_native_deadline_t *deadline)
{
    return native_wait_result(k_mutex_lock(mutex, deadline->timeout));
}

crux_status_t crux_mutex_impl_unlock(sync_native_mutex_t *mutex)
{
    return k_mutex_unlock(mutex) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t crux_mutex_impl_destroy(sync_native_mutex_t *mutex)
{
    (void)mutex; /* No dynamically allocated kernel state; common code proves quiescence. */
    return CRUX_OK;
}

/* Keep the public initial/maximum order at the native boundary. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
crux_status_t crux_semaphore_impl_create(sync_native_semaphore_t *semaphore, uint32_t initial,
                                         uint32_t maximum)
{
    semaphore->maximum = maximum;
    atomic_set(&semaphore->tokens, (atomic_val_t)initial);
    return k_sem_init(&semaphore->object, initial, maximum) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t crux_semaphore_impl_take(sync_native_semaphore_t *semaphore,
                                       const sync_native_deadline_t *deadline)
{
    int result = k_sem_take(&semaphore->object, deadline->timeout);
    if (result == 0) {
        (void)atomic_dec(&semaphore->tokens);
    }
    return native_wait_result(result);
}

/*
 * k_sem_give() at the limit drops the token silently, and nothing serialises givers. A give
 * therefore reserves its token in the counter before k_sem_give(), and a take removes its token
 * after k_sem_take(). The counter never exceeds the maximum and is at least the kernel count
 * plus the reserved gives, so the kernel count is below its limit whenever k_sem_give() runs.
 * A give can report full while a take that has already consumed a token has not yet returned.
 */
crux_status_t crux_semaphore_impl_give(sync_native_semaphore_t *semaphore)
{
    for (;;) {
        atomic_val_t tokens = atomic_get(&semaphore->tokens);
        if (tokens >= (atomic_val_t)semaphore->maximum) {
            return CRUX_ERR_BUSY;
        }
        if (atomic_cas(&semaphore->tokens, tokens, tokens + 1)) {
            break;
        }
    }
    k_sem_give(&semaphore->object);
    return CRUX_OK;
}

crux_status_t crux_semaphore_impl_destroy(sync_native_semaphore_t *semaphore)
{
    (void)semaphore;
    return CRUX_OK;
}
