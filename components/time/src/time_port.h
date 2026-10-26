#ifndef CRUX_TIME_PORT_H
#define CRUX_TIME_PORT_H

#include <crux/status.h>
#include <crux/time.h>

crux_status_t crux_time_port_init(void);
/* Lock exists before init; provider operations must not re-enter Time. */
bool crux_time_port_is_thread(void);
crux_status_t crux_time_port_lock(void);
void crux_time_port_unlock(void);
crux_status_t crux_time_port_read(crux_realtime_t *out);
crux_status_t crux_time_port_write(const crux_realtime_t *value);

#endif
