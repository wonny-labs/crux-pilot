#include "queue_port.h"
#include "interrupt.h"
#include <errno.h>
#include <limits.h>

K_MUTEX_DEFINE(crux_queue_metadata);
static struct k_condvar changed[4];
static bool prepared[4];

static crux_status_t status(int error, bool poll)
{
    if (error == 0)
        return CRUX_OK;
    if (error == -EBUSY || error == -EAGAIN)
        return poll ? CRUX_ERR_BUSY : CRUX_ERR_TIMEOUT;
    return CRUX_ERR_IO;
}

bool queue_port_context(void)
{
    return !crux_is_in_isr();
}

crux_status_t queue_port_deadline(uint32_t ms, queue_deadline_t *out)
{
    out->poll = ms == 0;
    if (out->poll) {
        out->timeout = K_NO_WAIT;
        return CRUX_OK;
    }
    int64_t now = k_uptime_ticks();
    int64_t duration = k_ms_to_ticks_ceil64(ms);
    if (now < 0 || duration < 0 || now >= INT64_MAX - duration - 1)
        return CRUX_ERR_OUT_OF_RANGE;
    out->timeout = K_TIMEOUT_ABS_TICKS(now + duration + 1);
    return CRUX_OK;
}

crux_status_t queue_port_lock(const queue_deadline_t *deadline)
{
    if (deadline == NULL)
        return status(k_mutex_lock(&crux_queue_metadata, K_FOREVER), false);
    return status(k_mutex_lock(&crux_queue_metadata, deadline->timeout), deadline->poll);
}

void queue_port_unlock(void)
{
    (void)k_mutex_unlock(&crux_queue_metadata);
}

crux_status_t queue_port_prepare(size_t index)
{
    if (!prepared[index]) {
        int result = k_condvar_init(&changed[index]);
        if (result != 0)
            return CRUX_ERR_IO;
        prepared[index] = true;
    }
    return CRUX_OK;
}

crux_status_t queue_port_wait(size_t index, const queue_deadline_t *deadline)
{
    int result = k_condvar_wait(&changed[index], &crux_queue_metadata, deadline->timeout);
    /* Pinned Zephyr reacquires the mutex only on successful wake. Retain the
     * admitted lifetime reference while regaining it for timeout bookkeeping.
     * K_FOREVER on this persistent valid mutex cannot time out. This final
     * bookkeeping may delay observed return, as documented by the contract. */
    if (result != 0) {
        (void)k_mutex_lock(&crux_queue_metadata, K_FOREVER);
    }
    return status(result, false);
}

void queue_port_notify(size_t index)
{
    (void)k_condvar_broadcast(&changed[index]);
}
