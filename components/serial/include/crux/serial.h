#ifndef CRUX_SERIAL_H
#define CRUX_SERIAL_H
#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t crux_serial_port_t;
/* Reserved representation; zero is invalid. Logical port zero is valid. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_serial_t;
typedef enum {
    CRUX_SERIAL_PARITY_NONE,
    CRUX_SERIAL_PARITY_EVEN,
    CRUX_SERIAL_PARITY_ODD
} crux_serial_parity_t;
typedef struct {
    uint32_t baud_rate;
    uint8_t data_bits;
    uint8_t stop_bits;
    crux_serial_parity_t parity;
} crux_serial_config_t;
/* Task context, one read + one write concurrently; same-direction overlap BUSY.
 * Borrow buffers until return. Short OK is progress, not complete delivery.
 * Zero poll => BUSY without progress; finite 1..INT32_MAX monotonic milliseconds.
 * Failed read preserves buffer; mandatory count is zero on error. Write accepts
 * into a queue, not the wire. Close is abortive; it consumes even on native error
 * but preserves a live handle on BUSY. See the approved Serial design. */
crux_status_t crux_serial_open(crux_serial_port_t port, const crux_serial_config_t *config,
                               crux_serial_t *out);
crux_status_t crux_serial_read(crux_serial_t serial, void *buffer, size_t capacity,
                               uint32_t timeout_ms, size_t *transferred);
crux_status_t crux_serial_write(crux_serial_t serial, const void *buffer, size_t size,
                                uint32_t timeout_ms, size_t *transferred);
crux_status_t crux_serial_close(crux_serial_t *serial);
#ifdef __cplusplus
}
#endif
#endif
