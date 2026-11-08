#define DT_DRV_COMPAT vnd_crux_spi_flash_emulator

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/spi_emul.h>
#include <zephyr/sys/util.h>

#include "flash_emulator.h"

/*
 * Answers the JEDEC identification command (0x9F) with three configured bytes. Anything else
 * leaves the data line idle high (0xFF) and is counted, so the test can show the consumer sent
 * exactly the four-byte command in one exchange.
 */
struct flash_data {
    uint8_t id[3];
    int native_result;
    int exchanges;
    int unrecognised;
    uint32_t operation;
};

static struct flash_data data_0;

static int flash_io(const struct emul *target, const struct spi_config *config,
                    const struct spi_buf_set *tx_bufs, const struct spi_buf_set *rx_bufs)
{
    struct flash_data *data = target->data;
    data->exchanges++;
    data->operation = config->operation;
    if (data->native_result != 0) {
        return data->native_result;
    }
    if (tx_bufs == NULL || rx_bufs == NULL || tx_bufs->count != 1 || rx_bufs->count != 1 ||
        tx_bufs->buffers[0].len != rx_bufs->buffers[0].len) {
        return -EINVAL;
    }

    const uint8_t *tx = tx_bufs->buffers[0].buf;
    uint8_t *rx = rx_bufs->buffers[0].buf;
    const size_t length = tx_bufs->buffers[0].len;
    memset(rx, 0xFF, length);
    if (length == 4 && tx[0] == 0x9F) {
        memcpy(&rx[1], data->id, sizeof(data->id));
    } else {
        data->unrecognised++;
    }
    return 0;
}

static const struct spi_emul_api bus_api = {.io = flash_io};

static void test_set_id(uint8_t manufacturer, uint8_t memory_type, uint8_t capacity_code)
{
    data_0.id[0] = manufacturer;
    data_0.id[1] = memory_type;
    data_0.id[2] = capacity_code;
}

static void test_set_native_result(int result)
{
    data_0.native_result = result;
}

static int test_exchange_count(void)
{
    return data_0.exchanges;
}

static int test_unrecognised_count(void)
{
    return data_0.unrecognised;
}

static uint32_t test_operation(void)
{
    return data_0.operation;
}

static const struct crux_spi_flash_emulator_test_api backend_api = {
    .set_id = test_set_id,
    .set_native_result = test_set_native_result,
    .exchange_count = test_exchange_count,
    .unrecognised_count = test_unrecognised_count,
    .operation = test_operation,
};

static int emul_init(const struct emul *target, const struct device *parent)
{
    ARG_UNUSED(target);
    ARG_UNUSED(parent);
    test_set_id(0xEF, 0x40, 0x17);
    return 0;
}

#define CRUX_SPI_FLASH_EMUL(n)                                                                     \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, &data_##n, NULL, POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,   \
                          NULL);                                                                   \
    EMUL_DT_INST_DEFINE(n, emul_init, &data_##n, NULL, &bus_api, &backend_api)

DT_INST_FOREACH_STATUS_OKAY(CRUX_SPI_FLASH_EMUL)
