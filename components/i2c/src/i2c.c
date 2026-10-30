#include "i2c_port.h"
#include <crux/i2c.h>

crux_status_t crux_i2c_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                uint8_t *rx_data, size_t rx_length)
{
    if (address > UINT16_C(0x7f)) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    if ((tx_length == 0 && rx_length == 0) ||
        (tx_length != 0 && tx_data == NULL) ||
        (rx_length != 0 && rx_data == NULL)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
#if SIZE_MAX > UINT32_MAX
    if (tx_length > UINT32_MAX || rx_length > UINT32_MAX) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
#endif
    return crux_i2c_impl_transfer(address, tx_data, tx_length, rx_data, rx_length);
}
