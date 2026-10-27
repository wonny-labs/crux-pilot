#ifndef CRUX_MONOTONIC_PORT_H
#define CRUX_MONOTONIC_PORT_H

#include <crux/time.h>
#include <stdbool.h>

crux_status_t crux_monotonic_impl_read_ns(int64_t *out);
/* Sample once and return a conservative upper bound for the unobserved fraction
 * of that sample's clock quantum. No realtime or RTC dependency.
 */
crux_status_t crux_monotonic_impl_read_upper_ns(int64_t *out);
crux_status_t crux_time_ticks_to_ns(int64_t ticks, uint32_t rate, bool round_up, int64_t *out);

#endif
