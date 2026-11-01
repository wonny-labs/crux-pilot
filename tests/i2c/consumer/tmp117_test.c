/*
 * TMP117 consumer tests against a test-only I2C provider that models the sensor's 16-bit
 * registers and records each transfer. The provider replaces the shipped loopback provider, which
 * cannot model a register device or inject failures. It is not linked into any product target.
 */
#include "tmp117.h"

#include "i2c_port.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static struct {
    int calls;
    uint16_t address;
    size_t tx_length;
    uint8_t tx_first;
    size_t rx_length;
    uint16_t registers[16];
    crux_status_t inject;
} fake;

crux_status_t crux_i2c_impl_transfer(uint16_t address, const uint8_t *tx_data, size_t tx_length,
                                     uint8_t *rx_data, size_t rx_length)
{
    fake.calls++;
    fake.address = address;
    fake.tx_length = tx_length;
    fake.tx_first = tx_length != 0 ? tx_data[0] : 0;
    fake.rx_length = rx_length;
    if (fake.inject != CRUX_OK) {
        return fake.inject;
    }
    if (address != TMP117_ADDRESS_GND || tx_length != 1 || rx_length != 2 || tx_data[0] > 0x0F) {
        return CRUX_ERR_IO;
    }
    rx_data[0] = (uint8_t)(fake.registers[tx_data[0]] >> 8);
    rx_data[1] = (uint8_t)(fake.registers[tx_data[0]] & 0xFF);
    return CRUX_OK;
}

static void reset(void)
{
    fake.calls = 0;
    fake.inject = CRUX_OK;
    for (size_t i = 0; i < sizeof(fake.registers) / sizeof(fake.registers[0]); ++i) {
        fake.registers[i] = 0;
    }
    fake.registers[0x0F] = UINT16_C(0x0117);
}

static void temperature_is(uint16_t raw, crux_status_t expected_status, int32_t expected_value)
{
    reset();
    fake.registers[0x00] = raw;
    int32_t value = INT32_C(0x5A5A5A5A);
    assert(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value) == expected_status);
    assert(fake.calls == 1);
    assert(value == (expected_status == CRUX_OK ? expected_value : INT32_C(0x5A5A5A5A)));
}

static void conversion_uses_one_128th_of_a_degree_steps(void)
{
    temperature_is(UINT16_C(0x0C80), CRUX_OK, 25000); /* 3200 LSB = 25 degrees */
    temperature_is(UINT16_C(0x0000), CRUX_OK, 0);
    temperature_is(UINT16_C(0x0001), CRUX_OK, 7);      /* 7.8125, truncated toward zero */
    temperature_is(UINT16_C(0xFFFF), CRUX_OK, -7);     /* -7.8125, truncated toward zero */
    temperature_is(UINT16_C(0xFFC0), CRUX_OK, -500);   /* -64 LSB */
    temperature_is(UINT16_C(0xE480), CRUX_OK, -55000); /* lower end of the rated range */
    temperature_is(UINT16_C(0x4B00), CRUX_OK, 150000); /* upper end of the rated range */
    temperature_is(UINT16_C(0x7FFF), CRUX_OK, 255992); /* largest register value */
}

static void no_conversion_marker_is_reported_as_no_data(void)
{
    temperature_is(UINT16_C(0x8000), CRUX_ERR_NO_DATA, 0);
    temperature_is(UINT16_C(0x8001), CRUX_OK, -255992); /* one LSB above the marker is data */
}

static void one_combined_transfer_selects_the_register_and_reads_two_bytes(void)
{
    reset();
    fake.registers[0x00] = UINT16_C(0x0C80);
    int32_t value = 0;
    assert(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value) == CRUX_OK);
    assert(fake.calls == 1);
    assert(fake.address == TMP117_ADDRESS_GND);
    assert(fake.tx_length == 1 && fake.tx_first == 0x00);
    assert(fake.rx_length == 2);

    assert(tmp117_probe(TMP117_ADDRESS_GND) == CRUX_OK);
    assert(fake.calls == 2);
    assert(fake.tx_length == 1 && fake.tx_first == 0x0F);
    assert(fake.rx_length == 2);
}

static void probe_checks_the_device_id_and_ignores_the_revision(void)
{
    reset();
    fake.registers[0x0F] = UINT16_C(0x1117); /* revision 1 */
    assert(tmp117_probe(TMP117_ADDRESS_GND) == CRUX_OK);
    fake.registers[0x0F] = UINT16_C(0x0118); /* different part */
    assert(tmp117_probe(TMP117_ADDRESS_GND) == CRUX_ERR_IO);
    fake.registers[0x0F] = UINT16_C(0x0000);
    assert(tmp117_probe(TMP117_ADDRESS_GND) == CRUX_ERR_IO);
    fake.registers[0x0F] = UINT16_C(0xF117); /* only the low 12 bits identify the part */
    assert(tmp117_probe(TMP117_ADDRESS_GND) == CRUX_OK);
}

static void transfer_statuses_are_returned_unchanged_and_output_is_untouched(void)
{
    const crux_status_t statuses[] = {CRUX_ERR_IO, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT,
                                      CRUX_ERR_NOT_SUPPORTED, CRUX_ERR_NOT_READY};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        reset();
        fake.inject = statuses[i];
        int32_t value = INT32_C(0x1234);
        assert(tmp117_read_millidegrees(TMP117_ADDRESS_GND, &value) == statuses[i]);
        assert(value == INT32_C(0x1234));
        assert(tmp117_probe(TMP117_ADDRESS_GND) == statuses[i]);
    }
}

static void unanswered_address_is_an_io_error(void)
{
    reset();
    int32_t value = 0;
    assert(tmp117_read_millidegrees(UINT16_C(0x49), &value) == CRUX_ERR_IO);
    assert(tmp117_probe(UINT16_C(0x49)) == CRUX_ERR_IO);
}

static void invalid_arguments_never_reach_the_provider(void)
{
    reset();
    int32_t value = 0;
    assert(tmp117_read_millidegrees(TMP117_ADDRESS_GND, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(tmp117_read_millidegrees(UINT16_C(0x80), &value) == CRUX_ERR_OUT_OF_RANGE);
    assert(tmp117_probe(UINT16_C(0x80)) == CRUX_ERR_OUT_OF_RANGE);
    assert(fake.calls == 0);
}

int main(void)
{
    conversion_uses_one_128th_of_a_degree_steps();
    no_conversion_marker_is_reported_as_no_data();
    one_combined_transfer_selects_the_register_and_reads_two_bytes();
    probe_checks_the_device_id_and_ignores_the_revision();
    transfer_statuses_are_returned_unchanged_and_output_is_untouched();
    unanswered_address_is_an_io_error();
    invalid_arguments_never_reach_the_provider();
    return 0;
}
