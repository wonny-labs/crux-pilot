#include "i2c_port.h"

/* Deterministic loopback for portable consumer tests; it does not emulate a bus. */
crux_status_t crux_i2c_impl_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                    uint8_t *rx_data, size_t rx_length)
{
    (void)address;
    for (size_t i = 0; i < rx_length; ++i) {
        rx_data[i] = i < tx_length ? tx_data[i] : 0;
    }
    return CRUX_OK;
}
