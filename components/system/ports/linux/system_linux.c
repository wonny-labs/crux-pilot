#include "interrupt.h"
#include "system_port.h"
#include <limits.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t metadata = PTHREAD_MUTEX_INITIALIZER;
static bool started, expired;
static int64_t last_feed;
static uint32_t timeout;

bool system_port_context_allowed(void)
{
    return !crux_is_in_isr();
}

crux_status_t system_port_lock(void)
{
    return pthread_mutex_lock(&metadata) == 0 ? CRUX_OK : CRUX_ERR_IO;
}

void system_port_unlock(void)
{
    (void)pthread_mutex_unlock(&metadata);
}

bool system_port_watchdog_emulated(void)
{
    return true;
}

crux_status_t system_port_watchdog_validate(uint32_t ms)
{
    /* This emulated profile mirrors the initial RT1060 subset only. */
    if (ms < 500 || ms > 64000)
        return CRUX_ERR_OUT_OF_RANGE;
    return ms % 500 == 0 ? CRUX_OK : CRUX_ERR_NOT_SUPPORTED;
}

static crux_status_t monotonic_ms(int64_t *out)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0 || value.tv_sec < 0 || value.tv_nsec < 0 ||
        value.tv_nsec >= 1000000000L || (uint64_t)value.tv_sec > (uint64_t)INT64_MAX / 1000u)
        return CRUX_ERR_IO;
    int64_t seconds_ms = (int64_t)value.tv_sec * 1000;
    int64_t fraction = value.tv_nsec / 1000000L;
    if (seconds_ms > INT64_MAX - fraction)
        return CRUX_ERR_IO;
    *out = seconds_ms + fraction;
    return CRUX_OK;
}

static crux_status_t observe(int64_t *now)
{
    if (monotonic_ms(now) != CRUX_OK || *now < last_feed)
        return CRUX_ERR_IO;
    if (*now - last_feed >= timeout)
        expired = true;
    return CRUX_OK;
}

crux_status_t system_port_watchdog_probe(system_watchdog_probe_t *out)
{
    if (started) {
        int64_t now;
        if (observe(&now) != CRUX_OK)
            return CRUX_ERR_IO;
    }
    *out = (system_watchdog_probe_t){started && !expired, expired};
    return CRUX_OK;
}

crux_status_t system_port_watchdog_start(uint32_t ms)
{
    if (started || monotonic_ms(&last_feed) != CRUX_OK)
        return CRUX_ERR_IO;
    timeout = ms;
    started = true;
    return CRUX_OK;
}

crux_status_t system_port_watchdog_feed(void)
{
    if (!started)
        return CRUX_ERR_IO;
    int64_t now;
    if (observe(&now) != CRUX_OK)
        return CRUX_ERR_IO;
    if (expired)
        return CRUX_ERR_TIMEOUT;
    last_feed = now;
    return CRUX_OK;
}

crux_status_t system_port_reset_status(void)
{
    return CRUX_ERR_NOT_SUPPORTED;
}

crux_status_t system_port_reset(void)
{
    return CRUX_ERR_NOT_SUPPORTED;
}

bool system_port_reset_cause_supported(void)
{
    return false;
}

crux_status_t system_port_reset_cause_get(crux_reset_cause_t *out)
{
    (void)out;
    return CRUX_ERR_NOT_SUPPORTED;
}

crux_status_t system_port_reset_cause_clear(void)
{
    return CRUX_ERR_NOT_SUPPORTED;
}
