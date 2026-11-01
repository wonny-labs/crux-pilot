#define DT_DRV_COMPAT vnd_crux_i2c_test_emulator

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/sys/util.h>

#include "i2c_emulator.h"

struct test_data {
    uint8_t registers[256];
    uint8_t selected_register;
    int message_count;
    bool repeated_start;
};

static struct test_data data_0;

static int test_transfer(const struct emul *target, struct i2c_msg *messages, int count,
                         int address)
{
    struct test_data *data = target->data;
    if (address != DT_INST_REG_ADDR(0) || count < 1 || count > 2) {
        return -ENXIO;
    }

    data->message_count = count;
    data->repeated_start = count == 2 && (messages[1].flags & I2C_MSG_RESTART) != 0;
    if (count == 2 && !data->repeated_start) {
        return -EINVAL;
    }

    if ((messages[0].flags & I2C_MSG_READ) == 0) {
        if (messages[0].len == 0) {
            return -EINVAL;
        }
        if (messages[0].buf[0] == 0xEE) {
            return -ENOTSUP;
        }
        data->selected_register = messages[0].buf[0];
        for (size_t i = 1; i < messages[0].len; ++i) {
            data->registers[data->selected_register++] = messages[0].buf[i];
        }
    }

    for (int m = 0; m < count; ++m) {
        if ((messages[m].flags & I2C_MSG_READ) != 0) {
            for (size_t i = 0; i < messages[m].len; ++i) {
                messages[m].buf[i] = data->registers[data->selected_register++];
            }
        }
    }
    return 0;
}

static const struct i2c_emul_api bus_api = {.transfer = test_transfer};

static int test_message_count(void)
{
    return data_0.message_count;
}

static bool test_used_repeated_start(void)
{
    return data_0.repeated_start;
}

static const struct crux_i2c_emulator_test_api backend_api = {
    .message_count = test_message_count,
    .used_repeated_start = test_used_repeated_start,
};

static int emul_init(const struct emul *target, const struct device *parent)
{
    ARG_UNUSED(target);
    ARG_UNUSED(parent);
    return 0;
}

#define CRUX_I2C_TEST_EMUL(n)                                                                      \
    static struct test_data data_##n;                                                              \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &data_##n, NULL, POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,   \
                          NULL);                                                                   \
    EMUL_DT_INST_DEFINE(n, emul_init, &data_##n, NULL, &bus_api, &backend_api)

DT_INST_FOREACH_STATUS_OKAY(CRUX_I2C_TEST_EMUL)
