#ifndef CRUX_THREAD_SLEEP_PORT_H
#define CRUX_THREAD_SLEEP_PORT_H

#include <crux/thread.h>
#include <stdbool.h>

/* May wake early; common code rechecks the original absolute nanosecond deadline. */
crux_status_t crux_thread_impl_sleep_until_ns(int64_t deadline_ns);
/* Prepare finite native absolute ticks, 0..INT64_MAX-1. No clamping/FOREVER;
 * checked conversion and reserved-value rejection preserve output on failure.
 */
crux_status_t crux_thread_ns_to_ticks_ceil(int64_t ns, uint32_t rate, int64_t *out_ticks);

#endif
