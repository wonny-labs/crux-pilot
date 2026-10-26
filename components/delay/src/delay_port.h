#ifndef CRUX_DELAY_PORT_H
#define CRUX_DELAY_PORT_H
#include <crux/status.h>
#include <stdint.h>

/* Native provider preflights its own encoding/headroom before waiting, without
 * Crux locks, allocation, scheduler sleep or delay-wide interrupt masking. */
crux_status_t crux_delay_port_get_max_ns(int64_t *out);
crux_status_t crux_delay_port_wait_ns(int64_t duration_ns);
#endif
