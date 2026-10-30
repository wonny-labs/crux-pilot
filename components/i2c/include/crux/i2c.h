#ifndef CRUX_I2C_H
#define CRUX_I2C_H

#include <stddef.h>
#include <stdint.h>
#include <crux/status.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transfer bytes to and/or from one 7-bit target on the composed I2C controller.
 * With both directions present, the Zephyr provider submits one write followed
 * by a read and requests a repeated start. The selected driver must honor that
 * request for this form to satisfy repeated-start semantics. At least one
 * direction must be non-empty. Each direction is limited to the range
 * representable by Zephyr's uint32_t message length.
 *
 * The call is synchronous. Buffers are borrowed until return and are never
 * retained. On failure, read-buffer contents and write progress at the target
 * are unspecified; a failed transfer does not roll back bytes already sent.
 * When both directions are present, their memory regions must not overlap.
 * No Crux deadline is provided; blocking and timeout behavior follow the native
 * driver. Transfer serialization is provider-specific; Crux makes no concurrent
 * call guarantee and adds no lock.
 */
crux_status_t crux_i2c_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                uint8_t *rx_data, size_t rx_length);

#ifdef __cplusplus
}
#endif

#endif
