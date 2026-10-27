/** @file
 * @brief Bounded serial byte-stream operations.
 *
 * Thread context only. timeout_ms uses 0 for polling and 1..INT32_MAX for
 * relative monotonic milliseconds. Buffers are borrowed until return.
 */
#ifndef CRUX_SERIAL_H
#define CRUX_SERIAL_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Logical port selected by platform composition; port 0 is valid. */
typedef uint32_t crux_serial_port_t;

/** Zero-invalid value handle. Do not construct or inspect reserved fields. */
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

/** Open an exclusively owned logical serial port.
 * @param port Bound logical port identifier.
 * @param config Requested UART settings; unsupported combinations are rejected.
 * @param[out] out Handle set on success, invalid on failure.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_serial_open(crux_serial_port_t port, const crux_serial_config_t *config,
                               crux_serial_t *out);

/** Read up to capacity bytes; a short read may return CRUX_OK.
 * @param serial Valid handle; one read and one write may overlap.
 * @param[out] buffer Caller-owned destination, borrowed for the call.
 * @param capacity Available destination bytes.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @param[out] transferred Mandatory byte count, zero on error.
 * @return CRUX_OK on success (check transferred), CRUX_ERR_BUSY or
 * CRUX_ERR_TIMEOUT when unavailable, or another crux_status_t error.
 */
crux_status_t crux_serial_read(crux_serial_t serial, void *buffer, size_t capacity,
                               uint32_t timeout_ms, size_t *transferred);

/** Queue up to size bytes; a short write may return CRUX_OK.
 * @param serial Valid handle; overlapping writes return CRUX_ERR_BUSY.
 * @param buffer Caller-owned source, borrowed for the call.
 * @param size Requested byte count.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @param[out] transferred Mandatory byte count, zero on error.
 * @return CRUX_OK means bytes were accepted, not transmitted on the wire.
 * Check transferred and retry remaining bytes if required by the caller.
 */
crux_status_t crux_serial_write(crux_serial_t serial, const void *buffer, size_t size,
                                uint32_t timeout_ms, size_t *transferred);

/** Abort pending buffered data and close the serial port.
 * @param[in,out] serial Retained on CRUX_ERR_BUSY; accepted close consumes it,
 * including when native cleanup fails.
 * @return CRUX_OK, CRUX_ERR_BUSY or a crux_status_t error.
 */
crux_status_t crux_serial_close(crux_serial_t *serial);

#ifdef __cplusplus
}
#endif

#endif
