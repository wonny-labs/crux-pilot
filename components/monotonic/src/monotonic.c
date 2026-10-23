#include "monotonic_port.h"

#include <stddef.h>

crux_status_t crux_monotonic_now(int64_t *out_ms) {
    if (out_ms == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!crux_monotonic_port_is_thread()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    int64_t value;
    crux_status_t status = crux_monotonic_port_now(&value);
    if (status != CRUX_OK) {
        return status;
    }
    if (value < 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out_ms = value;
    return CRUX_OK;
}

crux_status_t crux_monotonic_sleep_until(int64_t deadline_ms) {
    if (deadline_ms < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!crux_monotonic_port_is_thread()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    int64_t now;
    crux_status_t status = crux_monotonic_now(&now);
    if (status != CRUX_OK) {
        return status;
    }
    if (deadline_ms > now && deadline_ms - now > INT32_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    while (now < deadline_ms) {
        status = crux_monotonic_port_wait_until(deadline_ms);
        if (status != CRUX_OK) {
            return status;
        }
        status = crux_monotonic_now(&now);
        if (status != CRUX_OK) {
            return status;
        }
    }
    return CRUX_OK;
}

crux_status_t crux_monotonic_ticks_to_ms(int64_t ticks, uint32_t rate, int64_t *out) {
    if (ticks < 0 || rate == 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    int64_t seconds = ticks / rate;
    int64_t fraction = (ticks % rate) * 1000 / rate;
    if (seconds > INT64_MAX / 1000 ||
        (seconds == INT64_MAX / 1000 && fraction > INT64_MAX % 1000)) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out = seconds * 1000 + fraction;
    return CRUX_OK;
}

crux_status_t crux_monotonic_ms_to_ticks_ceil(int64_t ms, uint32_t rate, int64_t *out) {
    if (ms < 0 || rate == 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    int64_t seconds = ms / 1000;
    int64_t fraction = ((ms % 1000) * rate + 999) / 1000;
    if (seconds > (INT64_MAX - fraction) / rate) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out = seconds * rate + fraction;
    return CRUX_OK;
}
