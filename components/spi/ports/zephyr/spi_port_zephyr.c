#include "spi_port.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>

#if !DT_HAS_ALIAS(crux_spi0)
#error "The Zephyr composition must provide the crux-spi0 alias"
#endif

#define CRUX_SPI_NODE DT_ALIAS(crux_spi0)

#if !DT_NODE_HAS_STATUS(CRUX_SPI_NODE, okay)
#error "The crux-spi0 alias must select an enabled SPI peripheral"
#endif

#if DT_PROP(CRUX_SPI_NODE, spi_hold_cs)
#error "The crux-spi0 peripheral must not request spi-hold-cs; Crux has no CS release operation"
#endif

static const struct spi_dt_spec peripheral = SPI_DT_SPEC_GET(CRUX_SPI_NODE, SPI_WORD_SET(8));

crux_status_t crux_spi_impl_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    if (!spi_is_ready_dt(&peripheral)) {
        return CRUX_ERR_NOT_READY;
    }

    struct spi_buf tx_buffer = {.buf = (void *)tx_data, .len = length};
    struct spi_buf rx_buffer = {.buf = rx_data, .len = length};
    const struct spi_buf_set tx_set = {.buffers = &tx_buffer, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx_buffer, .count = 1};

    return spi_transceive_dt(&peripheral, &tx_set, &rx_set) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
