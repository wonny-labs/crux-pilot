#include "monotonic_port.h"

#include <zephyr/kernel.h>

crux_status_t crux_monotonic_impl_read_ns(int64_t *out)
{
    return crux_time_ticks_to_ns(k_uptime_ticks(), CONFIG_SYS_CLOCK_TICKS_PER_SEC, false, out);
}

crux_status_t crux_monotonic_impl_read_upper_ns(int64_t *out)
{
    int64_t ticks = k_uptime_ticks();
    if (ticks < 0 || ticks == INT64_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    /* One sample: bound the unobserved part of its current tick, then round up
     * fractional nanoseconds. This is deadline creation, not pure arithmetic.
     */
    return crux_time_ticks_to_ns(ticks + 1, CONFIG_SYS_CLOCK_TICKS_PER_SEC, true, out);
}
