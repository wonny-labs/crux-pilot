#define DT_DRV_COMPAT vnd_crux_spi_test_emulator

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/sys/util.h>

#include "spi_emulator.h"

struct test_data {
    int calls;
    uint32_t frequency;
    uint32_t operation;
    uint16_t word_size;
    uint16_t slave;
};

static struct test_data data_0;

static int test_io(const struct emul *target, const struct spi_config *config,
                   const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
    struct test_data *data = target->data;
    data->calls++;
    data->frequency = config->frequency;
    data->operation = config->operation;
    data->word_size = SPI_WORD_SIZE_GET(config->operation);
    data->slave = config->slave;
    if (tx_bufs == NULL || rx_bufs == NULL || tx_bufs->count != 1 || rx_bufs->count != 1 ||
        tx_bufs->buffers[0].len != rx_bufs->buffers[0].len) {
        return -EINVAL;
    }
    const uint8_t *tx = tx_bufs->buffers[0].buf;
    uint8_t *rx = rx_bufs->buffers[0].buf;
    size_t length = tx_bufs->buffers[0].len;
    if (length != 0 && tx[0] == 0xFF) {
        return -EIO;
    }
    if (length != 0 && tx[0] == 0xEE) {
        return -ENOTSUP;
    }
    memcpy(rx, tx, length);
    return 0;
}

static const struct spi_emul_api bus_api = {.io = test_io};

static int test_call_count(void)
{
    return data_0.calls;
}
static uint32_t test_frequency(void)
{
    return data_0.frequency;
}
static uint32_t test_operation(void)
{
    return data_0.operation;
}
static uint16_t test_word_size(void)
{
    return data_0.word_size;
}
static uint16_t test_slave(void)
{
    return data_0.slave;
}

static const struct crux_spi_emulator_test_api backend_api = {
    .call_count = test_call_count,
    .frequency = test_frequency,
    .operation = test_operation,
    .word_size = test_word_size,
    .slave = test_slave,
};

static int emul_init(const struct emul *target, const struct device *parent)
{
    ARG_UNUSED(target);
    ARG_UNUSED(parent);
    return 0;
}

#define CRUX_SPI_TEST_EMUL(n)                                                                      \
    static struct test_data data_##n;                                                              \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &data_##n, NULL, POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,   \
                          NULL);                                                                   \
    EMUL_DT_INST_DEFINE(n, emul_init, &data_##n, NULL, &bus_api, &backend_api)

DT_INST_FOREACH_STATUS_OKAY(CRUX_SPI_TEST_EMUL)
