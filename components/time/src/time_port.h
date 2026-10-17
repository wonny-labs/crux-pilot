#ifndef CRUX_TIME_PORT_H
#define CRUX_TIME_PORT_H

#include <crux/status.h>
#include <crux/time.h>

crux_status_t crux_time_port_init(void);
crux_status_t crux_time_port_read(crux_time_instant_t *out);
crux_status_t crux_time_port_write(crux_time_instant_t value);

#endif
