#ifndef CRUX_MONOTONIC_PORT_H
#define CRUX_MONOTONIC_PORT_H
#include <crux/monotonic.h>
#include <stdbool.h>
bool crux_monotonic_port_is_thread(void);
crux_status_t crux_monotonic_port_now(int64_t *out_ms);
/* May wake early; the common predicate rechecks the original deadline. */
crux_status_t crux_monotonic_port_wait_until(int64_t deadline_ms);
/* Checked native tick conversion; output is preserved on failure. */
crux_status_t crux_monotonic_ticks_to_ms(int64_t ticks, uint32_t rate, int64_t *out);
crux_status_t crux_monotonic_ms_to_ticks_ceil(int64_t ms, uint32_t rate, int64_t *out);
#endif
