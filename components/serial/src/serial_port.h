#ifndef CRUX_SERIAL_PORT_H
#define CRUX_SERIAL_PORT_H
#include <crux/serial.h>
#include <stdbool.h>
#ifndef CRUX_SERIAL_PORT_COUNT
#define CRUX_SERIAL_PORT_COUNT 1
#endif
typedef struct {
    uint64_t end_ms;
    bool poll;
} serial_deadline_t;
bool crux_serial_context_allowed(void);
crux_status_t crux_serial_monotonic_now_ms(uint64_t *now);
/* NULL is the untimed final-bookkeeping lock on valid initialized metadata. */
crux_status_t serial_port_metadata_lock(const serial_deadline_t *deadline);
void serial_port_metadata_unlock(void);
crux_status_t crux_serial_impl_open(uint32_t port, const crux_serial_config_t *config);
crux_status_t crux_serial_impl_close(uint32_t port);
/* Native port owns the one deadline and latched device fault. Positive count is
 * an irrevocable commit => OK; errors leave read buffer and count untouched/zero.
 * Open/close run outside the common metadata lock; per-port state is permanent. */
crux_status_t crux_serial_impl_read(uint32_t port, void *buffer, size_t size,
                               const serial_deadline_t *deadline, size_t *count);
crux_status_t crux_serial_impl_write(uint32_t port, const void *buffer, size_t size,
                                const serial_deadline_t *deadline, size_t *count);
#endif
