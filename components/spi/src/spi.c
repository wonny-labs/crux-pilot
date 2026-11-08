#include "spi_port.h"
#include <crux/spi.h>

crux_status_t crux_spi_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    if (tx_data == NULL || rx_data == NULL || length == 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    return crux_spi_impl_transceive(tx_data, rx_data, length);
}
