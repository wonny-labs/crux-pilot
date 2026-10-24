#ifndef SERIAL_CONSUMER_RUNTIME_H
#define SERIAL_CONSUMER_RUNTIME_H

#include <crux/serial.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* This fixture owns its handle exclusively; calls must not overlap. The caller
 * chooses a fresh nonce for every exchange, including after reopen. A consumed
 * handle is cleared; retained_handle preserves ownership if cleanup returns BUSY.
 * transaction_status and cleanup_status remain separate even when both fail.
 * The return is transaction_status if it failed, otherwise cleanup_status.
 * Open/close have no timed bound; the budget limits admitted transfer work.
 * Result must not already contain a live retained handle. */
typedef struct {
    crux_status_t transaction_status;
    crux_status_t cleanup_status;
    crux_serial_t retained_handle;
} serial_runtime_result_t;

/* 16-byte request: c3 00 ff 51, little-endian nonce[8], 39 00 ff 5a.
 * Response is identical except byte 3 is 41. Malformed/stale response => IO.
 * No retransmission, resynchronisation, or delivery claim on failed exchange.
 * A valid response acknowledges peer receipt before abortive close. */
crux_status_t serial_runtime_transaction(crux_serial_port_t port,
                                         const crux_serial_config_t *config, uint64_t nonce,
                                         uint32_t budget_ms, serial_runtime_result_t *result);
crux_status_t serial_cpp_transaction(crux_serial_port_t port, const crux_serial_config_t *config,
                                     uint64_t nonce, uint32_t budget_ms,
                                     serial_runtime_result_t *result);
crux_status_t serial_cpp_zero_write(crux_serial_t live_handle);

#ifdef __cplusplus
}
#endif
#endif
