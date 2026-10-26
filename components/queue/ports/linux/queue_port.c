#include "queue_port.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
static pthread_mutex_t metadata = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed[4];
static bool prepared[4];
static crux_status_t status(int error)
{
    if (error == 0)
        return CRUX_OK;
    if (error == EBUSY)
        return CRUX_ERR_BUSY;
    if (error == ETIMEDOUT)
        return CRUX_ERR_TIMEOUT;
    return CRUX_ERR_IO;
}
bool queue_port_context(void)
{
    return true;
}
crux_status_t queue_port_deadline(uint32_t ms, queue_deadline_t *out)
{
    out->poll = ms == 0;
    if (out->poll)
        return CRUX_OK;
    if (clock_gettime(CLOCK_MONOTONIC, &out->absolute) != 0)
        return CRUX_ERR_IO;
    if (out->absolute.tv_sec < 0 || out->absolute.tv_nsec < 0 ||
        out->absolute.tv_nsec >= 1000000000L)
        return CRUX_ERR_IO;
    int64_t seconds = (int64_t)(ms / 1000U) + 1;
    if ((int64_t)out->absolute.tv_sec > INT64_MAX - seconds)
        return CRUX_ERR_OUT_OF_RANGE;
    out->absolute.tv_sec += (time_t)(ms / 1000U);
    out->absolute.tv_nsec += (long)(ms % 1000U) * 1000000L;
    if (out->absolute.tv_nsec >= 1000000000L) {
        ++out->absolute.tv_sec;
        out->absolute.tv_nsec -= 1000000000L;
    }
    return CRUX_OK;
}
crux_status_t queue_port_lock(const queue_deadline_t *deadline)
{
    int result;
    do {
        if (deadline == NULL)
            result = pthread_mutex_lock(&metadata);
        else if (deadline->poll)
            result = pthread_mutex_trylock(&metadata);
        else
            result = pthread_mutex_clocklock(&metadata, CLOCK_MONOTONIC, &deadline->absolute);
    } while (result == EINTR);
    return status(result);
}
void queue_port_unlock(void)
{
    (void)pthread_mutex_unlock(&metadata);
}
crux_status_t queue_port_prepare(size_t index)
{
    if (prepared[index])
        return CRUX_OK;
    pthread_condattr_t attr;
    int result = pthread_condattr_init(&attr);
    if (result != 0)
        return CRUX_ERR_IO;
    result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    if (result == 0)
        result = pthread_cond_init(&changed[index], &attr);
    (void)pthread_condattr_destroy(&attr);
    if (result == 0)
        prepared[index] = true;
    return status(result);
}
crux_status_t queue_port_wait(size_t index, const queue_deadline_t *deadline)
{
    int result;
    do {
        result = pthread_cond_timedwait(&changed[index], &metadata, &deadline->absolute);
    } while (result == EINTR);
    return status(result);
}
void queue_port_notify(size_t index)
{
    (void)pthread_cond_broadcast(&changed[index]);
}
