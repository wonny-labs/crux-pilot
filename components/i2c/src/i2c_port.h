#ifndef CRUX_I2C_PORT_H
#define CRUX_I2C_PORT_H

#include <crux/status.h>
#include <stddef.h>
#include <stdint.h>

crux_status_t crux_i2c_impl_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                     uint8_t *rx_data, size_t rx_length);

#endif
