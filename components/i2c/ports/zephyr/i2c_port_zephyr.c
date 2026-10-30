#include "../../src/i2c_port.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>

#if !DT_HAS_ALIAS(crux_i2c0)
#error "The Zephyr composition must provide the crux-i2c0 alias"
#endif

#define CRUX_I2C_NODE DT_ALIAS(crux_i2c0)

#if !DT_NODE_HAS_STATUS(CRUX_I2C_NODE, okay)
#error "The crux-i2c0 alias must select an enabled I2C controller"
#endif

static const struct device *const bus = DEVICE_DT_GET(CRUX_I2C_NODE);

crux_status_t crux_i2c_impl_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                     uint8_t *rx_data, size_t rx_length)
{
    if (!device_is_ready(bus)) {
        return CRUX_ERR_NOT_READY;
    }

    struct i2c_msg messages[2];
    uint8_t message_count = 0;

    if (tx_length != 0) {
        messages[message_count++] = (struct i2c_msg){
            .buf = (uint8_t *)tx_data,
            .len = (uint32_t)tx_length,
            .flags = 0,
        };
    }
    if (rx_length != 0) {
        messages[message_count++] = (struct i2c_msg){
            .buf = rx_data,
            .len = (uint32_t)rx_length,
            .flags = I2C_MSG_READ | I2C_MSG_STOP | (tx_length != 0 ? I2C_MSG_RESTART : 0),
        };
    } else if (tx_length != 0) {
        messages[0].flags |= I2C_MSG_STOP;
    }

    return i2c_transfer(bus, messages, message_count, address) == 0 ? CRUX_OK : CRUX_ERR_IO;
}
