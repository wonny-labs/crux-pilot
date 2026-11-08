#ifndef CRUX_SPI_PORT_H
#define CRUX_SPI_PORT_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

crux_status_t crux_spi_impl_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length);

#endif
