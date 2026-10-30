#ifndef CRUX_SPI_H
#define CRUX_SPI_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Exchange equal-length TX and RX byte buffers with the composed SPI peripheral.
 * This is one synchronous full-duplex transaction; peripheral configuration
 * (frequency, mode, word size, and chip select) belongs to platform composition.
 * The Zephyr provider rejects configuration that holds CS beyond this call.
 * Both buffers must be non-null, non-overlapping, and valid until return.
 *
 * A nonzero native result maps to CRUX_ERR_IO and does not report partial
 * progress. No Crux deadline or concurrency guarantee is provided: blocking,
 * timeout, chip-select handling, and call serialization are provider/driver
 * behavior. Crux adds no lock.
 */
crux_status_t crux_spi_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length);

#ifdef __cplusplus
}
#endif

#endif
