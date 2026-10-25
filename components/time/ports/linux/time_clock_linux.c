#include "time_clock_port.h"

#include <time.h>

bool crux_time_clock_is_thread(void) {
    return true;
}

static crux_status_t timespec_to_ns(const struct timespec *value, int64_t *out_ns) {
    const int64_t billion = INT64_C(1000000000);
    if (value->tv_nsec < 0 || value->tv_nsec >= billion) {
        return CRUX_ERR_IO;
    }
    if (value->tv_sec < 0 || (uintmax_t)value->tv_sec > (uintmax_t)(INT64_MAX / billion) ||
        ((uintmax_t)value->tv_sec == (uintmax_t)(INT64_MAX / billion) &&
         value->tv_nsec > INT64_MAX % billion)) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out_ns = (int64_t)value->tv_sec * billion + value->tv_nsec;
    return CRUX_OK;
}

crux_status_t crux_time_clock_read(int64_t *out_ns) {
    struct timespec sample;
    if (clock_gettime(CLOCK_MONOTONIC, &sample) != 0) {
        return CRUX_ERR_IO;
    }
    return timespec_to_ns(&sample, out_ns);
}

crux_status_t crux_time_clock_read_upper_bound(int64_t *out_ns) {
    struct timespec resolution;
    if (clock_getres(CLOCK_MONOTONIC, &resolution) != 0 || resolution.tv_sec < 0) {
        return CRUX_ERR_IO;
    }
    int64_t quantum_ns;
    crux_status_t status = timespec_to_ns(&resolution, &quantum_ns);
    if (status != CRUX_OK) {
        return status;
    }
    if (quantum_ns == 0) {
        return CRUX_ERR_IO;
    }
    int64_t sample_ns;
    status = crux_time_clock_read(&sample_ns);
    if (status != CRUX_OK) {
        return status;
    }
    crux_monotonic_time_t upper;
    status = crux_monotonic_add_duration((crux_monotonic_time_t){sample_ns},
                                         crux_duration_from_ns(quantum_ns), &upper);
    if (status == CRUX_OK) {
        *out_ns = upper.private_ns;
    }
    return status;
}
