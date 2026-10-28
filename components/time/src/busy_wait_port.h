#ifndef CRUX_BUSY_WAIT_PORT_H
#define CRUX_BUSY_WAIT_PORT_H
#include <crux/status.h>
#include <stdint.h>
/* Validate the provider's safe range before waiting; no public limit query. */
crux_status_t crux_busy_wait_ns(int64_t duration_ns);
#endif
