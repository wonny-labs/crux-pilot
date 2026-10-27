#ifndef CRUX_REALTIME_PORT_H
#define CRUX_REALTIME_PORT_H

#include <crux/status.h>
#include <crux/time.h>

crux_status_t crux_realtime_impl_init(void);
/* Lock exists before init; provider operations must not re-enter Time. */
crux_status_t crux_realtime_impl_lock(void);
void crux_realtime_impl_unlock(void);
crux_status_t crux_realtime_impl_read(crux_realtime_t *out);
crux_status_t crux_realtime_impl_write(const crux_realtime_t *time);

#endif
