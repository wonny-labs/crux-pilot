#include <crux/i2c.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
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

ZTEST(crux_i2c, test_unsupported_native_operation_maps_to_not_supported)
{
    /* The emulated target reports -ENOTSUP for a write whose first byte is the marker. */
    const uint8_t marker[] = {0xEE};

    zassert_equal(crux_i2c_transfer(0x32, marker, sizeof(marker), NULL, 0), CRUX_ERR_NOT_SUPPORTED);
}

ZTEST(crux_i2c, test_unimplemented_native_operation_maps_to_not_supported)
{
    /* The emulated target reports -ENOSYS for a write whose first byte is this marker. */
    const uint8_t marker[] = {0xED};

    zassert_equal(crux_i2c_transfer(0x32, marker, sizeof(marker), NULL, 0), CRUX_ERR_NOT_SUPPORTED);
}

static K_SEM_DEFINE(isr_done, 0, 1);
static volatile bool isr_ran_in_interrupt;
static volatile crux_status_t isr_status;
static volatile uint8_t isr_value;

static void isr_expiry(struct k_timer *timer)
{
    const uint8_t reg = 0x10;
    uint8_t value = 0x5A;

    ARG_UNUSED(timer);
    isr_ran_in_interrupt = k_is_in_isr();
    isr_status = crux_i2c_transfer(0x32, &reg, sizeof(reg), &value, sizeof(value));
    isr_value = value;
    k_sem_give(&isr_done);
}

static K_TIMER_DEFINE(isr_timer, isr_expiry, NULL);

ZTEST(crux_i2c, test_transfer_from_interrupt_context_is_rejected)
{
    k_timer_start(&isr_timer, K_MSEC(1), K_NO_WAIT);
    zassert_equal(k_sem_take(&isr_done, K_MSEC(500)), 0);
    zassert_true(isr_ran_in_interrupt, "the timer callback must run in interrupt context");
    zassert_equal(isr_status, CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(isr_value, 0x5A, "the read buffer must be untouched when rejected");
}

ZTEST_SUITE(crux_i2c, NULL, NULL, NULL, NULL, NULL);
