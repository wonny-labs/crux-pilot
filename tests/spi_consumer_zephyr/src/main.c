#include <crux/spi.h>
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/ztest.h>

#include "flash_emulator.h"
#include "spi_flash_id.h"

#define EMUL_NODE DT_NODELABEL(crux_flash)

static const struct crux_spi_flash_emulator_test_api *emulator(void)
{
    return EMUL_DT_GET(EMUL_NODE)->backend_api;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    emulator()->set_id(0xEF, 0x40, 0x17);
    emulator()->set_native_result(0);
}

ZTEST_SUITE(crux_spi_flash_id, NULL, NULL, before, NULL, NULL);

ZTEST(crux_spi_flash_id, test_identification_is_read_through_the_native_driver)
{
    struct spi_flash_jedec_id id = {0};
    uint32_t bytes = 0;

    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_OK);
    zassert_equal(id.manufacturer, 0xEF);
    zassert_equal(id.memory_type, 0x40);
    zassert_equal(id.capacity_code, 0x17);
    zassert_equal(spi_flash_capacity_bytes(&id, &bytes), CRUX_OK);
    zassert_equal(bytes, 8U * 1024U * 1024U);
}

ZTEST(crux_spi_flash_id, test_one_four_byte_exchange_per_read)
{
    struct spi_flash_jedec_id id = {0};
    const int before_count = emulator()->exchange_count();

    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_OK);
    zassert_equal(emulator()->exchange_count() - before_count, 1);
    zassert_equal(emulator()->unrecognised_count(), 0);
}

ZTEST(crux_spi_flash_id, test_composed_transfer_settings_are_mode_zero_msb_first_8_bit)
{
    struct spi_flash_jedec_id id = {0};

    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_OK);
    zassert_equal(SPI_WORD_SIZE_GET(emulator()->operation()), 8);
    zassert_equal(emulator()->operation() & (SPI_MODE_CPOL | SPI_MODE_CPHA | SPI_TRANSFER_LSB), 0U);
}

ZTEST(crux_spi_flash_id, test_idle_lines_are_an_io_error)
{
    struct spi_flash_jedec_id id = {1, 2, 3};

    emulator()->set_id(0xFF, 0xFF, 0xFF);
    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_ERR_IO);
    emulator()->set_id(0x00, 0x00, 0x00);
    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_ERR_IO);
    zassert_equal(id.manufacturer, 1);
}

ZTEST(crux_spi_flash_id, test_native_results_map_through_the_consumer)
{
    struct spi_flash_jedec_id id = {0};

    emulator()->set_native_result(-EIO);
    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_ERR_IO);
    emulator()->set_native_result(-ENOTSUP);
    zassert_equal(spi_flash_read_jedec_id(&id), CRUX_ERR_NOT_SUPPORTED);
}
