#define DT_DRV_COMPAT vnd_crux_tmp117_emulator

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/sys/util.h>

#include "tmp117_emulator.h"

/*
 * A TMP117 has sixteen 16-bit registers addressed by a pointer register. A read returns the
 * selected register most significant byte first. Anything other than a one-byte pointer write
 * followed by a restarted two-byte read is counted so the test can show the consumer used the
 * combined form.
 */
struct tmp117_data {
    uint16_t registers[16];
    int transfer_count;
    int malformed_count;
};

static struct tmp117_data data_0;

static int tmp117_transfer(const struct emul *target, struct i2c_msg *messages, int count,
                           int address)
{
    struct tmp117_data *data = target->data;
    if (address != DT_INST_REG_ADDR(0)) {
        return -ENXIO;
    }
    data->transfer_count++;

    const bool combined = count == 2 && (messages[0].flags & I2C_MSG_READ) == 0 &&
                          messages[0].len == 1 && (messages[1].flags & I2C_MSG_READ) != 0 &&
                          (messages[1].flags & I2C_MSG_RESTART) != 0 && messages[1].len == 2;
    if (!combined) {
        data->malformed_count++;
        return -EINVAL;
    }

    const uint8_t pointer = messages[0].buf[0];
    if (pointer >= ARRAY_SIZE(data->registers)) {
        return -EINVAL;
    }
    messages[1].buf[0] = (uint8_t)(data->registers[pointer] >> 8);
    messages[1].buf[1] = (uint8_t)(data->registers[pointer] & 0xFF);
    return 0;
}

static const struct i2c_emul_api bus_api = {.transfer = tmp117_transfer};

static void test_set_register(uint8_t reg, uint16_t value)
{
    data_0.registers[reg] = value;
}

static int test_transfer_count(void)
{
    return data_0.transfer_count;
}

static int test_malformed_count(void)
{
    return data_0.malformed_count;
}

static const struct crux_tmp117_emulator_test_api backend_api = {
    .set_register = test_set_register,
    .transfer_count = test_transfer_count,
    .malformed_count = test_malformed_count,
};

static int emul_init(const struct emul *target, const struct device *parent)
{
    ARG_UNUSED(parent);
    struct tmp117_data *data = target->data;
    data->registers[0x0F] = 0x0117;
    return 0;
}

#define CRUX_TMP117_EMUL(n)                                                                        \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &data_##n, NULL, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,   \
                          NULL);                                                                   \
    EMUL_DT_INST_DEFINE(n, emul_init, &data_##n, NULL, &bus_api, &backend_api)

DT_INST_FOREACH_STATUS_OKAY(CRUX_TMP117_EMUL)
