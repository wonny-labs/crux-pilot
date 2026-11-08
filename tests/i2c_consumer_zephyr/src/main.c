#include <crux/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#include "tmp117.h"
#include "tmp117_emulator.h"

#define EMUL_NODE DT_NODELABEL(crux_tmp117)

static const struct crux_tmp117_emulator_test_api *emulator(void)
{
    return EMUL_DT_GET(EMUL_NODE)->backend_api;
}

static void before(void *fixture)
{
    ARG_UNUSED(fixture);
    emulator()->set_register(0x00, 0);
    emulator()->set_register(0x0F, 0x0117);
}

ZTEST_SUITE(crux_tmp117, NULL, NULL, before, NULL, NULL);

ZTEST(crux_tmp117, test_probe_identifies_the_sensor)
{
    zassert_equal(tmp117_probe(TMP117_ADDRESS_GND), CRUX_OK);
    emulator()->set_register(0x0F, 0x0000);
    zassert_equal(tmp117_probe(TMP117_ADDRESS_GND), CRUX_ERR_IO);
}

ZTEST(crux_tmp117, test_temperature_is_converted_through_the_native_driver)
{
    int32_t value = 0;

    emulator()->set_register(0x00, 0x0C80);
    zassert_equal(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value), CRUX_OK);
    zassert_equal(value, 25000);

    emulator()->set_register(0x00, 0xFFC0);
    zassert_equal(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value), CRUX_OK);
    zassert_equal(value, -500);
}

ZTEST(crux_tmp117, test_unfinished_conversion_is_reported_as_no_data)
{
    int32_t value = 77;

    emulator()->set_register(0x00, 0x8000);
    zassert_equal(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value), CRUX_ERR_NO_DATA);
    zassert_equal(value, 77);
}

ZTEST(crux_tmp117, test_each_operation_is_one_combined_transfer)
{
    int32_t value = 0;
    const int before_count = emulator()->transfer_count();

    emulator()->set_register(0x00, 0x0C80);
    zassert_equal(tmp117_probe(TMP117_ADDRESS_GND), CRUX_OK);
    zassert_equal(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value), CRUX_OK);
    zassert_equal(emulator()->transfer_count() - before_count, 2);
    zassert_equal(emulator()->malformed_count(), 0);
}

ZTEST(crux_tmp117, test_absent_device_maps_the_native_error_to_io)
{
    int32_t value = 0;

    zassert_equal(tmp117_probe(0x49), CRUX_ERR_IO);
    zassert_equal(tmp117_read_millidegrees(0x49, &value), CRUX_ERR_IO);
}
