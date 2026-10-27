#include <crux/time_types.h>

#include <stddef.h>

/* Division bounds work for both signed signs without intermediate overflow. */
static crux_status_t duration_try_scale(int64_t value, int64_t factor, crux_duration_t *out)
{
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (value > INT64_MAX / factor || value < INT64_MIN / factor) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->private_ns = value * factor;
    return CRUX_OK;
}

crux_status_t crux_duration_try_from_us(int64_t us, crux_duration_t *out)
{
    return duration_try_scale(us, INT64_C(1000), out);
}

crux_status_t crux_duration_try_from_ms(int64_t ms, crux_duration_t *out)
{
    return duration_try_scale(ms, INT64_C(1000000), out);
}

crux_status_t crux_duration_try_from_sec(int64_t seconds, crux_duration_t *out)
{
    return duration_try_scale(seconds, INT64_C(1000000000), out);
}

crux_status_t crux_monotonic_add(crux_monotonic_time_t base, crux_duration_t delta,
                                 crux_monotonic_time_t *out)
{
    if (out == NULL || base.private_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if ((delta.private_ns > 0 && base.private_ns > INT64_MAX - delta.private_ns) ||
        (delta.private_ns < 0 && base.private_ns <= -(delta.private_ns + INT64_C(1)))) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    out->private_ns = base.private_ns + delta.private_ns;
    return CRUX_OK;
}

crux_duration_t crux_monotonic_diff(crux_monotonic_time_t a, crux_monotonic_time_t b)
{
    crux_duration_t result = {a.private_ns - b.private_ns};
    return result;
}
