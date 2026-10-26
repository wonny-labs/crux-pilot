#include "thread_sleep_port.h"

#include <crux/time.h>
#include <stddef.h>

crux_status_t thread_sleep_ns_to_ticks_ceil(int64_t ns, uint32_t rate, int64_t *out_ticks)
{
    if (out_ticks == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (ns < 0 || rate == 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    const int64_t billion = INT64_C(1000000000);
    int64_t seconds = ns / billion;
    int64_t product = (ns % billion) * rate;
    int64_t fraction = product / billion + (product % billion != 0 ? 1 : 0);
    /* The native 64-bit absolute timeout reserves INT64_MAX. */
    if (seconds > (INT64_MAX - 1 - fraction) / rate) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out_ticks = seconds * rate + fraction;
    return CRUX_OK;
}

crux_status_t crux_thread_sleep_until(crux_monotonic_time_t deadline)
{
    if (deadline.private_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!thread_native_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    for (;;) {
        crux_monotonic_time_t now;
        crux_status_t status = crux_time_get_monotonic(&now);
        if (status != CRUX_OK) {
            return status;
        }
        if (crux_monotonic_is_at_or_before(deadline, now)) {
            return CRUX_OK;
        }
        status = thread_native_sleep_until(deadline.private_ns);
        if (status != CRUX_OK) {
            return status;
        }
    }
}

crux_status_t crux_thread_sleep_for(crux_duration_t duration)
{
    if (duration.private_ns < 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (!thread_native_context_allowed()) {
        return CRUX_ERR_NOT_SUPPORTED;
    }
    if (duration.private_ns == 0) {
        return CRUX_OK;
    }
    crux_monotonic_time_t deadline;
    crux_status_t status = crux_time_make_deadline_after(duration, &deadline);
    return status == CRUX_OK ? crux_thread_sleep_until(deadline) : status;
}
