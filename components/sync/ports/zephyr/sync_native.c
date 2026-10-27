#include "interrupt.h"
#include "sync_internal.h"
#include <errno.h>
#include <limits.h>

K_MUTEX_DEFINE(sync_metadata);

bool sync_native_context_allowed(void)
{
    return !crux_is_in_isr();
}
void sync_native_metadata_lock(void)
{
    (void)k_mutex_lock(&sync_metadata, K_FOREVER);
}
crux_status_t sync_native_metadata_lock_until(const sync_native_deadline_t *deadline)
{
    int result = k_mutex_lock(&sync_metadata, deadline->timeout);
    if (result == 0)
        return CRUX_OK;
    if (result == -EAGAIN)
        return CRUX_ERR_TIMEOUT;
    if (result == -EBUSY)
        return CRUX_ERR_BUSY;
    return CRUX_ERR_IO;
}
void sync_native_metadata_unlock(void)
{
    (void)k_mutex_unlock(&sync_metadata);
}

crux_status_t sync_native_deadline(uint32_t timeout_ms, sync_native_deadline_t *out)
{
    if (timeout_ms > INT32_MAX)
        return CRUX_ERR_OUT_OF_RANGE;
    out->poll = timeout_ms == 0;
    if (timeout_ms == 0) {
        out->timeout = K_NO_WAIT;
        return CRUX_OK;
    }
    int64_t now = k_uptime_ticks();
    int64_t duration = k_ms_to_ticks_ceil64(timeout_ms);
    /* One extra tick covers the unobserved fraction of the current tick. */
    if (now > INT64_MAX - duration - 1)
        return CRUX_ERR_OUT_OF_RANGE;
    out->timeout = K_TIMEOUT_ABS_TICKS(now + duration + 1);
    return CRUX_OK;
}
static crux_status_t native_wait_result(int result)
{
    if (result == 0)
        return CRUX_OK;
    if (result == -EBUSY)
        return CRUX_ERR_BUSY;
    if (result == -EAGAIN)
        return CRUX_ERR_TIMEOUT;
    return CRUX_ERR_IO;
}
sync_native_identity_t sync_native_current(void)
{
    return k_current_get();
}
bool sync_native_identity_equal(sync_native_identity_t first, sync_native_identity_t second)
{
    return first == second;
}
crux_status_t sync_native_mutex_create(sync_native_mutex_t *mutex)
{
    return k_mutex_init(mutex) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t sync_native_mutex_lock(sync_native_mutex_t *mutex,
                                     const sync_native_deadline_t *deadline)
{
    return native_wait_result(k_mutex_lock(mutex, deadline->timeout));
}
crux_status_t sync_native_mutex_unlock(sync_native_mutex_t *mutex)
{
    return k_mutex_unlock(mutex) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t sync_native_mutex_destroy(sync_native_mutex_t *mutex)
{
    (void)mutex; /* No dynamically allocated kernel state; common code proves quiescence. */
    return CRUX_OK;
}
/* Keep the public initial/maximum order at the native boundary. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
crux_status_t sync_native_semaphore_create(sync_native_semaphore_t *semaphore, uint32_t initial,
                                           uint32_t maximum)
{
    semaphore->maximum = maximum;
    return k_sem_init(&semaphore->object, initial, maximum) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
crux_status_t sync_native_semaphore_take(sync_native_semaphore_t *semaphore,
                                         const sync_native_deadline_t *deadline)
{
    return native_wait_result(k_sem_take(&semaphore->object, deadline->timeout));
}
crux_status_t sync_native_semaphore_give(sync_native_semaphore_t *semaphore)
{
    /* Common metadata protection serialises all givers. Takes can only reduce count. */
    if (k_sem_count_get(&semaphore->object) == semaphore->maximum)
        return CRUX_ERR_BUSY;
    k_sem_give(&semaphore->object);
    return CRUX_OK;
}
crux_status_t sync_native_semaphore_destroy(sync_native_semaphore_t *semaphore)
{
    (void)semaphore;
    return CRUX_OK;
}
