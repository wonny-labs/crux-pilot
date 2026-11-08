#include "spi_port.h"

/* Deterministic loopback for portable consumer tests; it does not emulate a bus. */
crux_status_t crux_spi_impl_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        rx_data[i] = tx_data[i];
    }
    return CRUX_OK;
}
