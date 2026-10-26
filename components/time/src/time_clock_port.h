#ifndef CRUX_TIME_CLOCK_PORT_H
#define CRUX_TIME_CLOCK_PORT_H

#include <crux/time.h>
#include <stdbool.h>

bool crux_time_clock_is_thread(void);
crux_status_t crux_time_clock_read(int64_t *out_ns);
/* Sample once and return a conservative upper bound for the unobserved fraction
 * of that sample's clock quantum. No realtime or RTC dependency.
 */
crux_status_t crux_time_clock_read_upper_bound(int64_t *out_ns);
crux_status_t crux_time_ticks_to_ns(int64_t ticks, uint32_t rate, bool round_up, int64_t *out_ns);

#endif
