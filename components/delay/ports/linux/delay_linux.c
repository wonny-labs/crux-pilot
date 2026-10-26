#include "delay_port.h"
#include <errno.h>
#include <stddef.h>
#include <time.h>

#define NS_PER_SECOND INT64_C(1000000000)

static crux_status_t native_error(void) {
    return errno == ENOSYS || errno == ENOTSUP ? CRUX_ERR_NOT_SUPPORTED : CRUX_ERR_IO;
}

static crux_status_t timespec_ns(const struct timespec *value, int64_t *out) {
    if (value->tv_nsec < 0 || value->tv_nsec >= NS_PER_SECOND)
        return CRUX_ERR_IO;
    if (value->tv_sec < 0 || value->tv_sec > INT64_MAX / NS_PER_SECOND)
        return CRUX_ERR_OUT_OF_RANGE;
    int64_t whole = (int64_t)value->tv_sec * NS_PER_SECOND;
    if (value->tv_nsec > INT64_MAX - whole)
        return CRUX_ERR_OUT_OF_RANGE;
    *out = whole + value->tv_nsec;
    return CRUX_OK;
}

static crux_status_t read_now(int64_t *out) {
    /* This Linux profile uses the native signed64/ns monotonic counter range. */
    if (sizeof(time_t) != sizeof(int64_t) || (time_t)-1 > 0)
        return CRUX_ERR_NOT_SUPPORTED;
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return native_error();
    return timespec_ns(&value, out);
}

static crux_status_t read_upper_bound(int64_t *out) {
    int64_t now;
    crux_status_t status = read_now(&now);
    if (status != CRUX_OK)
        return status;
    struct timespec resolution;
    if (clock_getres(CLOCK_MONOTONIC, &resolution) != 0)
        return native_error();
    int64_t quantum;
    status = timespec_ns(&resolution, &quantum);
    if (status != CRUX_OK)
        return status;
    if (quantum <= 0)
        return CRUX_ERR_IO;
    if (quantum > INT64_MAX - now)
        return CRUX_ERR_OUT_OF_RANGE;
    *out = now + quantum;
    return CRUX_OK;
}

crux_status_t crux_delay_port_get_max_ns(int64_t *out) {
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    int64_t upper;
    crux_status_t status = read_upper_bound(&upper);
    if (status == CRUX_OK)
        *out = INT64_MAX - upper;
    return status;
}

crux_status_t crux_delay_port_wait_ns(int64_t duration_ns) {
    if (duration_ns < 0)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (duration_ns == 0)
        return CRUX_OK;
    int64_t upper;
    crux_status_t status = read_upper_bound(&upper);
    if (status != CRUX_OK)
        return status;
    if (duration_ns > INT64_MAX - upper)
        return CRUX_ERR_OUT_OF_RANGE;
    const int64_t deadline = upper + duration_ns;
    for (;;) {
        int64_t now;
        status = read_now(&now);
        if (status != CRUX_OK)
            return status;
        if (now >= deadline)
            return CRUX_OK;
    }
}
