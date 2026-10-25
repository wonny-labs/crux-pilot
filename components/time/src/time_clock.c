#include "time_clock_port.h"

#include <stddef.h>

crux_status_t crux_time_get_monotonic(crux_monotonic_time_t *out) {
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!crux_time_clock_is_thread()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    int64_t ns;
    crux_status_t status = crux_time_clock_read(&ns);
    if (status != CRUX_OK) {
        return status;
    }
    if (ns < 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out = (crux_monotonic_time_t){ns};
    return CRUX_OK;
}

crux_status_t crux_time_make_deadline_after(crux_duration_t min_duration,
                                            crux_monotonic_time_t *out_deadline) {
    if (out_deadline == NULL || min_duration.private_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (min_duration.private_ns == 0) {
        return crux_time_get_monotonic(out_deadline);
    }
    if (!crux_time_clock_is_thread()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    int64_t upper_ns;
    crux_status_t status = crux_time_clock_read_upper_bound(&upper_ns);
    if (status != CRUX_OK) {
        return status;
    }
    if (upper_ns < 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    return crux_monotonic_add_duration((crux_monotonic_time_t){upper_ns}, min_duration,
                                       out_deadline);
}

crux_status_t crux_time_ticks_to_ns(int64_t ticks, uint32_t rate, bool round_up, int64_t *out_ns) {
    if (out_ns == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (ticks < 0 || rate == 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    const int64_t billion = INT64_C(1000000000);
    int64_t seconds = ticks / rate;
    /* rate is uint32_t, so the remainder product fits signed int64_t. */
    int64_t product = (ticks % rate) * billion;
    int64_t fraction = product / rate;
    if (round_up && product % rate != 0) {
        ++fraction;
    }
    if (seconds > (INT64_MAX - fraction) / billion) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out_ns = seconds * billion + fraction;
    return CRUX_OK;
}
