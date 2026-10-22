#include "execution_internal.h"
#include <errno.h>

static crux_status_t native_result(int result) {
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
exec_native_identity_t exec_native_current(void) {
    return pthread_self();
}
bool exec_native_identity_equal(exec_native_identity_t first, exec_native_identity_t second) {
    return pthread_equal(first, second) != 0;
}
crux_status_t exec_native_mutex_create(exec_native_mutex_t *mutex) {
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
crux_status_t exec_native_mutex_lock(exec_native_mutex_t *mutex,
                                     const exec_native_deadline_t *deadline) {
    int result;
    do {
        result = deadline->poll
                     ? pthread_mutex_trylock(mutex)
                     : pthread_mutex_clocklock(mutex, CLOCK_MONOTONIC, &deadline->absolute);
    } while (result == EINTR);
    return native_result(result);
}
crux_status_t exec_native_mutex_unlock(exec_native_mutex_t *mutex) {
    return native_result(pthread_mutex_unlock(mutex));
}
crux_status_t exec_native_mutex_destroy(exec_native_mutex_t *mutex) {
    return native_result(pthread_mutex_destroy(mutex));
}
/* Keep the public initial/maximum order at the native boundary. */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
crux_status_t exec_native_semaphore_create(exec_native_semaphore_t *semaphore, uint32_t initial,
                                           uint32_t maximum) {
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
crux_status_t exec_native_semaphore_take(exec_native_semaphore_t *semaphore,
                                         const exec_native_deadline_t *deadline) {
    /* Polls may serialize bookkeeping, as admission already does. Only an
     * empty token count may produce BUSY; internal contention is not emptiness. */
    int result = deadline->poll ? pthread_mutex_lock(&semaphore->lock)
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
crux_status_t exec_native_semaphore_give(exec_native_semaphore_t *semaphore) {
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
crux_status_t exec_native_semaphore_destroy(exec_native_semaphore_t *semaphore) {
    int result = pthread_cond_destroy(&semaphore->changed);
    if (result == 0)
        result = pthread_mutex_destroy(&semaphore->lock);
    return native_result(result);
}
