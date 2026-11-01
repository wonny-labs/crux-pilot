/*
 * Portable-layer contract tests against a test-only provider. The provider models a register
 * device and can inject failures; it replaces the shipped loopback provider, which always
 * succeeds and cannot exercise error paths. It is not linked into any product target.
 */
#include <crux/i2c.h>

#include "i2c_port.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEVICE_ADDRESS UINT16_C(0x32)

static struct {
    int calls;
    uint16_t address;
    const uint8_t *tx_data;
    size_t tx_length;
    uint8_t *rx_data;
    size_t rx_length;
    crux_status_t inject;
    uint8_t registers[256];
} fake;

crux_status_t crux_i2c_impl_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                     uint8_t *rx_data, size_t rx_length)
{
    fake.calls++;
    fake.address = address;
    fake.tx_data = tx_data;
    fake.tx_length = tx_length;
    fake.rx_data = rx_data;
    fake.rx_length = rx_length;
    if (fake.inject != CRUX_OK) {
        return fake.inject;
    }
    if (address != DEVICE_ADDRESS) {
        return CRUX_ERR_IO; /* no device acknowledges this address */
    }
    uint8_t selected = 0;
    if (tx_length != 0) {
        selected = tx_data[0];
        for (size_t i = 1; i < tx_length; ++i) {
            fake.registers[selected++] = tx_data[i];
        }
    }
    for (size_t i = 0; i < rx_length; ++i) {
        rx_data[i] = fake.registers[selected++];
    }
    return CRUX_OK;
}

static void reset(void)
{
    fake.calls = 0;
    fake.inject = CRUX_OK;
}

static void invalid_requests_never_reach_the_provider(void)
{
    uint8_t byte = 0;

    reset();
    assert(crux_i2c_transfer(0x80, &byte, 1, NULL, 0) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_i2c_transfer(DEVICE_ADDRESS, NULL, 0, NULL, 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_i2c_transfer(DEVICE_ADDRESS, NULL, 1, NULL, 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_i2c_transfer(DEVICE_ADDRESS, NULL, 0, NULL, 1) == CRUX_ERR_INVALID_ARGUMENT);
#if SIZE_MAX > UINT32_MAX
    size_t too_long = (size_t)UINT32_MAX + 1U;
    assert(crux_i2c_transfer(DEVICE_ADDRESS, &byte, too_long, NULL, 0) == CRUX_ERR_OUT_OF_RANGE);
    assert(crux_i2c_transfer(DEVICE_ADDRESS, NULL, 0, &byte, too_long) == CRUX_ERR_OUT_OF_RANGE);
#endif
    assert(fake.calls == 0);
}

static void valid_forms_reach_the_provider_unchanged(void)
{
    const uint8_t write[] = {0x10, 0xA5};
    uint8_t reg = 0x10;
    uint8_t value = 0;

    reset();
    assert(crux_i2c_transfer(DEVICE_ADDRESS, write, sizeof(write), NULL, 0) == CRUX_OK);
    assert(fake.calls == 1 && fake.address == DEVICE_ADDRESS);
    assert(fake.tx_data == write && fake.tx_length == sizeof(write));
    assert(fake.rx_data == NULL && fake.rx_length == 0);

    assert(crux_i2c_transfer(DEVICE_ADDRESS, &reg, 1, &value, 1) == CRUX_OK);
    assert(fake.calls == 2 && fake.tx_length == 1 && fake.rx_length == 1);
    assert(value == 0xA5);

    value = 0;
    reg = 0x10;
    assert(crux_i2c_transfer(DEVICE_ADDRESS, NULL, 0, &value, 1) == CRUX_OK);
    assert(fake.calls == 3 && fake.tx_data == NULL && fake.tx_length == 0);
}

static void provider_failures_are_returned_unchanged(void)
{
    const crux_status_t injected[] = {CRUX_ERR_NOT_READY, CRUX_ERR_IO, CRUX_ERR_NOT_SUPPORTED};
    uint8_t byte = 0x10;

    for (size_t i = 0; i < sizeof(injected) / sizeof(injected[0]); ++i) {
        reset();
        fake.inject = injected[i];
        assert(crux_i2c_transfer(DEVICE_ADDRESS, &byte, 1, &byte, 1) == injected[i]);
        assert(fake.calls == 1);
    }
}

static void absent_device_is_an_io_failure(void)
{
    uint8_t byte = 0;

    reset();
    assert(crux_i2c_transfer(0x33, NULL, 0, &byte, 1) == CRUX_ERR_IO);
    assert(fake.calls == 1);
}

int main(void)
{
    invalid_requests_never_reach_the_provider();
    valid_forms_reach_the_provider_unchanged();
    provider_failures_are_returned_unchanged();
    absent_device_is_an_io_failure();
    return 0;
}
