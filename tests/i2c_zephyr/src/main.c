#include <crux/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#include "i2c_emulator.h"

#define EMUL_NODE DT_NODELABEL(crux_i2c_test_target)

static const struct crux_i2c_emulator_test_api *emulator_api(void)
{
    const struct emul *emulator = EMUL_DT_GET(EMUL_NODE);
    return emulator->backend_api;
}

ZTEST(crux_i2c, test_write_and_repeated_start_read)
{
    const uint8_t write[] = {0x10, 0xA5};
    uint8_t reg = 0x10;
    uint8_t value = 0;

    zassert_equal(crux_i2c_transfer(0x32, write, sizeof(write), NULL, 0), CRUX_OK);
    zassert_equal(crux_i2c_transfer(0x32, &reg, sizeof(reg), &value, sizeof(value)), CRUX_OK);
    zassert_equal(value, 0xA5);
    zassert_equal(emulator_api()->message_count(), 2);
    zassert_true(emulator_api()->used_repeated_start());
}

ZTEST(crux_i2c, test_unmatched_target_maps_native_error)
{
    uint8_t value = 0;
    zassert_equal(crux_i2c_transfer(0x33, NULL, 0, &value, sizeof(value)), CRUX_ERR_IO);
}

ZTEST(crux_i2c, test_read_only_transfer)
{
    const uint8_t write[] = {0x11, 0x5A};
    uint8_t reg = 0x11;
    uint8_t value = 0;

    zassert_equal(crux_i2c_transfer(0x32, write, sizeof(write), NULL, 0), CRUX_OK);
    zassert_equal(crux_i2c_transfer(0x32, &reg, sizeof(reg), NULL, 0), CRUX_OK);
    zassert_equal(crux_i2c_transfer(0x32, NULL, 0, &value, sizeof(value)), CRUX_OK);
    zassert_equal(value, 0x5A);
    zassert_equal(emulator_api()->message_count(), 1);
    zassert_false(emulator_api()->used_repeated_start());
}

ZTEST_SUITE(crux_i2c, NULL, NULL, NULL, NULL, NULL);
