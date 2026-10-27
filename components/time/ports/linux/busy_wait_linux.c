#include "busy_wait_port.h"
#include <errno.h>
#include <stddef.h>
#include <time.h>

#define NS_PER_SECOND INT64_C(1000000000)

_Static_assert(sizeof(time_t) == sizeof(int64_t) && (time_t)-1 < 0,
               "Enabled CPU Busy-wait requires signed 64-bit time_t");

static crux_status_t native_error(void)
{
    return errno == ENOSYS || errno == ENOTSUP ? CRUX_ERR_NOT_SUPPORTED : CRUX_ERR_IO;
}

static crux_status_t timespec_ns(const struct timespec *value, int64_t *out)
{
    if (value->tv_nsec < 0 || value->tv_nsec >= NS_PER_SECOND)
        return CRUX_ERR_IO;
    if (value->tv_sec < 0 || value->tv_sec > INT64_MAX / NS_PER_SECOND)
        return CRUX_ERR_OUT_OF_RANGE;
    int64_t whole_ns = (int64_t)value->tv_sec * NS_PER_SECOND;
    if (value->tv_nsec > INT64_MAX - whole_ns)
        return CRUX_ERR_OUT_OF_RANGE;
    *out = whole_ns + value->tv_nsec;
    return CRUX_OK;
}

static crux_status_t read_now(int64_t *out_ns)
{
    /* The build rejects unsupported native counter representations. */
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return native_error();
    return timespec_ns(&value, out_ns);
}

static crux_status_t read_upper_bound(int64_t *out_ns)
{
    int64_t now_ns;
    crux_status_t status = read_now(&now_ns);
    if (status != CRUX_OK)
        return status;
    struct timespec resolution;
    if (clock_getres(CLOCK_MONOTONIC, &resolution) != 0)
        return native_error();
    int64_t quantum_ns;
    status = timespec_ns(&resolution, &quantum_ns);
    if (status != CRUX_OK)
        return status;
    if (quantum_ns <= 0)
        return CRUX_ERR_IO;
    if (quantum_ns > INT64_MAX - now_ns)
        return CRUX_ERR_OUT_OF_RANGE;
    *out_ns = now_ns + quantum_ns;
    return CRUX_OK;
}

crux_status_t crux_busy_wait_impl_ns(int64_t duration_ns)
{
    if (duration_ns < 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (duration_ns == 0)
        return CRUX_OK;
    int64_t upper_ns;
    crux_status_t status = read_upper_bound(&upper_ns);
    if (status != CRUX_OK)
        return status;
    if (duration_ns > INT64_MAX - upper_ns)
        return CRUX_ERR_OUT_OF_RANGE;
    /* Re-observe after preflight so elapsed validation cannot consume headroom. */
    status = read_upper_bound(&upper_ns);
    if (status != CRUX_OK) {
        return status;
    }
    if (duration_ns > INT64_MAX - upper_ns) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    const int64_t deadline_ns = upper_ns + duration_ns;
    for (;;) {
        int64_t now_ns;
        status = read_now(&now_ns);
        if (status != CRUX_OK)
            return status;
        if (now_ns >= deadline_ns)
            return CRUX_OK;
    }
}
