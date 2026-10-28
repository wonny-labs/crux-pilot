#include "interrupt.h"
#include "sync_internal.h"

#include <errno.h>
#include <limits.h>

static pthread_mutex_t sync_metadata = PTHREAD_MUTEX_INITIALIZER;

_Static_assert(sizeof(time_t) == sizeof(int64_t), "Host provider requires 64-bit time_t");

bool crux_sync_context_allowed(void)
{
    return !crux_is_in_isr();
}

void sync_native_metadata_lock(void)
{
    (void)pthread_mutex_lock(&sync_metadata);
}

crux_status_t sync_native_metadata_lock_until(const sync_native_deadline_t *deadline)
{
    int result = deadline->poll ? pthread_mutex_trylock(&sync_metadata)
                                : pthread_mutex_clocklock(&sync_metadata, CLOCK_MONOTONIC,
                                                          &deadline->absolute);
    if (result == 0) {
        return CRUX_OK;
    }
    if (result == ETIMEDOUT) {
        return CRUX_ERR_TIMEOUT;
    }
    return result == EBUSY ? CRUX_ERR_BUSY : CRUX_ERR_IO;
}

void sync_native_metadata_unlock(void)
{
    (void)pthread_mutex_unlock(&sync_metadata);
}

crux_status_t crux_sync_make_deadline(uint32_t timeout_ms, sync_native_deadline_t *out)
{
    if (timeout_ms > INT32_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->poll = timeout_ms == 0;
    if (clock_gettime(CLOCK_MONOTONIC, &out->absolute) != 0) {
        return CRUX_ERR_IO;
    }
    long nanoseconds = out->absolute.tv_nsec + (long)(timeout_ms % 1000) * 1000000L;
    int64_t seconds = timeout_ms / 1000 + nanoseconds / 1000000000L;
    if (out->absolute.tv_sec < 0 || out->absolute.tv_sec > INT64_MAX - seconds) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->absolute.tv_sec += seconds;
    out->absolute.tv_nsec = nanoseconds % 1000000000L;
    return CRUX_OK;
}

static crux_status_t native_result(int result)
{
    switch (result) {
    case 0:
        return CRUX_OK;
    case EBUSY:
        return CRUX_ERR_BUSY;
    case ETIMEDOUT:
        return CRUX_ERR_TIMEOUT;
    case ENOMEM:
    case EAGAIN:
        return CRUX_ERR_NO_RESOURCES;
    default:
        return CRUX_ERR_IO;
    }
}
sync_native_identity_t crux_sync_current_identity(void)
{
    return pthread_self();
}
bool crux_sync_identity_equal(sync_native_identity_t first, sync_native_identity_t second)
{
    return pthread_equal(first, second) != 0;
}
crux_status_t crux_mutex_impl_create(sync_native_mutex_t *mutex)
{
    pthread_mutexattr_t attributes;
    int result = pthread_mutexattr_init(&attributes);
    if (result != 0)
        return native_result(result);
    result = pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK);
    if (result == 0)
        result = pthread_mutex_init(mutex, &attributes);
    (void)pthread_mutexattr_destroy(&attributes);
    return native_result(result);
}
crux_status_t crux_mutex_impl_lock(sync_native_mutex_t *mutex,
                                     const sync_native_deadline_t *deadline)
{
    int result;
    do {
        result = deadline->poll
                     ? pthread_mutex_trylock(mutex)
                     : pthread_mutex_clocklock(mutex, CLOCK_MONOTONIC, &deadline->absolute);
    } while (result == EINTR);
    return native_result(result);
}
crux_status_t crux_mutex_impl_unlock(sync_native_mutex_t *mutex)
{
    return native_result(pthread_mutex_unlock(mutex));
}
crux_status_t crux_mutex_impl_destroy(sync_native_mutex_t *mutex)
{
    return native_result(pthread_mutex_destroy(mutex));
}
/* Keep the public initial/maximum order at the native boundary. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
crux_status_t crux_semaphore_impl_create(sync_native_semaphore_t *semaphore, uint32_t initial,
                                           uint32_t maximum)
{
    int result = pthread_mutex_init(&semaphore->lock, NULL);
    if (result != 0)
        return native_result(result);
    pthread_condattr_t attributes;
    result = pthread_condattr_init(&attributes);
    if (result != 0) {
        (void)pthread_mutex_destroy(&semaphore->lock);
        return native_result(result);
    }
    result = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (result == 0)
        result = pthread_cond_init(&semaphore->changed, &attributes);
    (void)pthread_condattr_destroy(&attributes);
    if (result != 0) {
        (void)pthread_mutex_destroy(&semaphore->lock);
        return native_result(result);
    }
    semaphore->count = initial;
    semaphore->maximum = maximum;
    return CRUX_OK;
}
crux_status_t crux_semaphore_impl_take(sync_native_semaphore_t *semaphore,
                                         const sync_native_deadline_t *deadline)
{
    int result = deadline->poll ? pthread_mutex_trylock(&semaphore->lock)
                                : pthread_mutex_clocklock(&semaphore->lock, CLOCK_MONOTONIC,
                                                          &deadline->absolute);
    if (result != 0)
        return native_result(result);
    while (semaphore->count == 0) {
        if (deadline->poll) {
            result = EBUSY;
            break;
        }
        result = pthread_cond_timedwait(&semaphore->changed, &semaphore->lock, &deadline->absolute);
        if (result == ETIMEDOUT || (result != 0 && result != EINTR))
            break;
    }
    if (semaphore->count != 0 && (result == 0 || result == ETIMEDOUT)) {
        --semaphore->count;
        result = 0;
    }
    (void)pthread_mutex_unlock(&semaphore->lock);
    return native_result(result);
}
crux_status_t crux_semaphore_impl_give(sync_native_semaphore_t *semaphore)
{
    int result = pthread_mutex_lock(&semaphore->lock);
    if (result != 0)
        return native_result(result);
    if (semaphore->count == semaphore->maximum) {
        result = EBUSY;
    } else {
        ++semaphore->count;
        result = pthread_cond_signal(&semaphore->changed);
    }
    (void)pthread_mutex_unlock(&semaphore->lock);
    return native_result(result);
}
crux_status_t crux_semaphore_impl_destroy(sync_native_semaphore_t *semaphore)
{
    int result = pthread_cond_destroy(&semaphore->changed);
    if (result == 0)
        result = pthread_mutex_destroy(&semaphore->lock);
    return native_result(result);
}
