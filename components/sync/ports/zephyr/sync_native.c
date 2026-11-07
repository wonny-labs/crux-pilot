#include "interrupt.h"
#include "sync_internal.h"
#include <errno.h>
#include <limits.h>

/*
 * Metadata sections are short and never wait, so a locked scheduler protects them.
 * A metadata k_mutex would save and restore its holder's priority around the native
 * mutex operations inside those sections, which undoes priority inheritance.
 *
 * One exception: a native unlock that lowers the caller can leave a higher thread
 * selected, which an interrupt then switches to despite the lock. Common code writes
 * no slot state after that call, so the section is already finished by then.
 */
BUILD_ASSERT(!IS_ENABLED(CONFIG_SMP) && CONFIG_NUM_METAIRQ_PRIORITIES == 0,
             "A locked scheduler excludes other threads only on one CPU without meta-IRQ threads");

bool crux_sync_context_allowed(void)
{
    return !crux_is_in_isr() && !k_is_pre_kernel();
}

void sync_native_metadata_lock(void)
{
    k_sched_lock();
}

crux_status_t sync_native_metadata_lock_until(const sync_native_deadline_t *deadline)
{
    (void)deadline; /* Admission never waits: no thread is switched out inside a section. */
    k_sched_lock();
    return CRUX_OK;
}

void sync_native_metadata_unlock(void)
{
    k_sched_unlock();
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
    return k_sem_init(&semaphore->object, initial, maximum) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

crux_status_t crux_semaphore_impl_take(sync_native_semaphore_t *semaphore,
                                       const sync_native_deadline_t *deadline)
{
    return native_wait_result(k_sem_take(&semaphore->object, deadline->timeout));
}

crux_status_t crux_semaphore_impl_give(sync_native_semaphore_t *semaphore)
{
    /* Common metadata protection serialises all givers. Takes can only reduce count. */
    if (k_sem_count_get(&semaphore->object) == semaphore->maximum) {
        return CRUX_ERR_BUSY;
    }
    k_sem_give(&semaphore->object);
    return CRUX_OK;
}

crux_status_t crux_semaphore_impl_destroy(sync_native_semaphore_t *semaphore)
{
    (void)semaphore;
    return CRUX_OK;
}
