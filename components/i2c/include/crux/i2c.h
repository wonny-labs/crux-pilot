/**
 * @file
 * @brief Synchronous transfers with one 7-bit target on the composed I2C controller.
 *
 * Calls are synchronous and task-context only. A provider that can detect
 * interrupt context returns CRUX_ERR_NOT_SUPPORTED from a call made there. When
 * such a call also has an invalid argument, which of the two statuses is
 * returned is unspecified. The Host provider has no interrupt context and does
 * not check, and calls before the kernel is running (pre-kernel or early
 * startup) are not supported but are not detected. Crux adds no lock
 * and no deadline: blocking, timeout behavior and serialization of concurrent
 * calls follow the native driver, and separate calls are not an atomic
 * application-level transaction.
 */
#ifndef CRUX_I2C_H
#define CRUX_I2C_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transfer bytes to and/or from one 7-bit target.
 *
 * At least one direction must be non-empty. With both directions present, the
 * Zephyr provider submits one write followed by a read and requests a repeated
 * start; the selected driver must honor that request for this form to satisfy
 * repeated-start semantics. Each direction is limited to the range
 * representable by Zephyr's uint32_t message length.
 *
 * Buffers are borrowed until return and never retained. When both directions
 * are present their memory regions must not overlap. On failure the read
 * buffer contents and the write progress at the target are unspecified; a
 * failed transfer does not roll back bytes already sent. Reserved 7-bit
 * addresses (0x00-0x07 and 0x78-0x7F) are not rejected; avoiding them is the
 * caller's responsibility. Arguments are checked in this order: the address,
 * then an empty request or a NULL buffer, then the lengths.
 *
 * @param address Target address, 0x00-0x7F.
 * @param tx_data Bytes to write; may be NULL only when @p tx_length is 0.
 * @param tx_length Number of bytes to write.
 * @param[out] rx_data Buffer for bytes to read; may be NULL only when @p rx_length is 0.
 * @param rx_length Number of bytes to read.
 * @retval CRUX_OK The transfer completed.
 * @retval CRUX_ERR_INVALID_ARGUMENT Both lengths are 0, or a non-zero length has a NULL buffer.
 * @retval CRUX_ERR_OUT_OF_RANGE @p address exceeds 0x7F or a length exceeds UINT32_MAX.
 * @retval CRUX_ERR_NOT_READY The native controller is not ready.
 * @retval CRUX_ERR_NOT_SUPPORTED Called from interrupt context, or the native driver reports
 *         the operation as unsupported.
 * @retval CRUX_ERR_IO The native driver reported any other failure, including a missing
 *         acknowledgement, a bus error or a timeout.
 */
crux_status_t crux_i2c_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                uint8_t *rx_data, size_t rx_length);

#ifdef __cplusplus
}
#endif

#endif
