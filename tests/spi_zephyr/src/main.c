#include <crux/spi.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/ztest.h>

#include "spi_emulator.h"

#define EMUL_NODE DT_NODELABEL(crux_spi_test_target)

static const struct crux_spi_emulator_test_api *emulator_api(void)
{
    const struct emul *emulator = EMUL_DT_GET(EMUL_NODE);
    return emulator->backend_api;
}

ZTEST(crux_spi, test_transceive_and_composition_config)
{
    const uint8_t tx[] = {0x10, 0x20, 0x30};
    uint8_t rx[sizeof(tx)] = {0};
    int calls_before = emulator_api()->call_count();

    zassert_equal(crux_spi_transceive(tx, rx, sizeof(tx)), CRUX_OK);
    for (size_t i = 0; i < sizeof(tx); ++i) {
        zassert_equal(rx[i], tx[i]);
    }
    zassert_equal(emulator_api()->call_count(), calls_before + 1);
    zassert_equal(emulator_api()->frequency(), 500000);
    zassert_true(
        (emulator_api()->operation() & (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_LSB)) ==
        (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_LSB));
    zassert_equal(emulator_api()->word_size(), 8);
    zassert_equal(emulator_api()->slave(), 0);
}

ZTEST(crux_spi, test_native_error_maps_to_io)
{
    const uint8_t tx[] = {0xFF};
    uint8_t rx = 0;
    zassert_equal(crux_spi_transceive(tx, &rx, sizeof(tx)), CRUX_ERR_IO);
}

ZTEST(crux_spi, test_invalid_arguments)
{
    uint8_t byte = 0;
    zassert_equal(crux_spi_transceive(NULL, &byte, 1), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_spi_transceive(&byte, NULL, 1), CRUX_ERR_INVALID_ARGUMENT);
    zassert_equal(crux_spi_transceive(&byte, &byte, 0), CRUX_ERR_INVALID_ARGUMENT);
}

ZTEST_SUITE(crux_spi, NULL, NULL, NULL, NULL, NULL);
