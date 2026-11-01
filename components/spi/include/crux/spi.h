/**
 * @file
 * @brief Synchronous full-duplex byte exchange with the composed SPI peripheral.
 *
 * Calls are synchronous and task-context only. A provider that can detect
 * interrupt context returns CRUX_ERR_NOT_SUPPORTED from a call made there. When
 * such a call also has an invalid argument, which of the two statuses is
 * returned is unspecified. The Host provider has no interrupt context and does
 * not check, and calls before the kernel is running (pre-kernel or early
 * startup) are not supported but are not detected. Crux adds no lock
 * and no deadline: blocking, timeout behavior, chip-select handling and
 * serialization of concurrent calls follow the native driver.
 */
#ifndef CRUX_SPI_H
#define CRUX_SPI_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Exchange equal-length TX and RX byte buffers in one transaction.
 *
 * This is one synchronous full-duplex transaction. Peripheral configuration
 * (frequency, mode, word size and chip select) belongs to platform
 * composition, and the Zephyr provider rejects, at build time, a configuration
 * that holds chip select beyond this call. Both buffers must be non-null,
 * distinct, non-overlapping and valid until return; overlap is not checked.
 * A failed call reports no partial progress.
 *
 * @param tx_data Bytes to transmit; @p length bytes are read.
 * @param[out] rx_data Buffer for received bytes; @p length bytes are written.
 * @param length Number of bytes to exchange; must be greater than zero.
 * @retval CRUX_OK The transaction completed.
 * @retval CRUX_ERR_INVALID_ARGUMENT A buffer is NULL or @p length is zero.
 * @retval CRUX_ERR_NOT_READY The native peripheral is not ready.
 * @retval CRUX_ERR_NOT_SUPPORTED Called from interrupt context, or the native driver reports
 *         the operation as unsupported.
 * @retval CRUX_ERR_IO The native driver reported any other failure.
 */
crux_status_t crux_spi_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
