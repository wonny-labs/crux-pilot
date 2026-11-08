/*
 * Flash-ID consumer tests against a test-only SPI provider that models a serial NOR flash's
 * identification command and records each exchange. The provider replaces the shipped loopback
 * provider, which echoes the transmit bytes and cannot model a peripheral or inject failures. It is
 * not linked into any product target.
 */
#include "spi_flash_id.h"

#include "spi_port.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static struct {
    int calls;
    size_t length;
    uint8_t tx[8];
    uint8_t id[3];
    uint8_t first_byte_answer;
    crux_status_t inject;
} fake;

crux_status_t crux_spi_impl_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    fake.calls++;
    fake.length = length;
    memset(fake.tx, 0xAA, sizeof(fake.tx));
    memcpy(fake.tx, tx_data, length < sizeof(fake.tx) ? length : sizeof(fake.tx));
    if (fake.inject != CRUX_OK) {
        return fake.inject;
    }
    memset(rx_data, 0xFF, length); /* an unrecognised command leaves the line idle high */
    if (length == 4 && tx_data[0] == 0x9F) {
        rx_data[0] = fake.first_byte_answer; /* clocked while the command goes out */
        memcpy(&rx_data[1], fake.id, sizeof(fake.id));
    }
    return CRUX_OK;
}

static void reset(void)
{
    fake.calls = 0;
    fake.inject = CRUX_OK;
    fake.first_byte_answer = 0xFF;
    fake.id[0] = 0xEF; /* Winbond */
    fake.id[1] = 0x40;
    fake.id[2] = 0x17; /* 8 MiB */
}

static void one_exchange_sends_the_command_and_decodes_the_response(void)
{
    reset();
    struct spi_flash_jedec_id id = {0};
    assert(spi_flash_read_jedec_id(&id) == CRUX_OK);
    assert(fake.calls == 1);
    assert(fake.length == 4);
    assert(fake.tx[0] == 0x9F && fake.tx[1] == 0 && fake.tx[2] == 0 && fake.tx[3] == 0);
    assert(id.manufacturer == 0xEF && id.memory_type == 0x40 && id.capacity_code == 0x17);
}

static void the_byte_clocked_during_the_command_is_ignored(void)
{
    reset();
    for (unsigned int answer = 0; answer <= 0xFF; answer += 0x55) {
        fake.first_byte_answer = (uint8_t)answer;
        struct spi_flash_jedec_id id = {0};
        assert(spi_flash_read_jedec_id(&id) == CRUX_OK);
        assert(id.manufacturer == 0xEF && id.memory_type == 0x40 && id.capacity_code == 0x17);
    }
}

static void an_absent_or_idle_device_is_an_io_error_and_leaves_the_output_alone(void)
{
    const uint8_t patterns[][3] = {{0xFF, 0xFF, 0xFF}, {0x00, 0x00, 0x00}};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        reset();
        memcpy(fake.id, patterns[i], sizeof(fake.id));
        struct spi_flash_jedec_id id = {1, 2, 3};
        assert(spi_flash_read_jedec_id(&id) == CRUX_ERR_IO);
        assert(id.manufacturer == 1 && id.memory_type == 2 && id.capacity_code == 3);
    }
}

static void a_partly_idle_response_is_still_an_identification(void)
{
    /* Only an all-0x00 or all-0xFF answer is rejected; a real part may contain either value. */
    const uint8_t patterns[][3] = {{0xFF, 0xFF, 0xFE}, {0x00, 0x00, 0x01}, {0xFF, 0x00, 0xFF}};
    for (size_t i = 0; i < sizeof(patterns) / sizeof(patterns[0]); ++i) {
        reset();
        memcpy(fake.id, patterns[i], sizeof(fake.id));
        struct spi_flash_jedec_id id = {0};
        assert(spi_flash_read_jedec_id(&id) == CRUX_OK);
        assert(id.manufacturer == patterns[i][0] && id.memory_type == patterns[i][1] &&
               id.capacity_code == patterns[i][2]);
    }
}

static void provider_statuses_are_returned_unchanged_and_output_is_untouched(void)
{
    const crux_status_t statuses[] = {CRUX_ERR_IO, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT,
                                      CRUX_ERR_NOT_SUPPORTED, CRUX_ERR_NOT_READY};
    for (size_t i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        reset();
        fake.inject = statuses[i];
        struct spi_flash_jedec_id id = {7, 8, 9};
        assert(spi_flash_read_jedec_id(&id) == statuses[i]);
        assert(id.manufacturer == 7 && id.memory_type == 8 && id.capacity_code == 9);
    }
}

static void a_null_output_never_reaches_the_provider(void)
{
    reset();
    assert(spi_flash_read_jedec_id(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(fake.calls == 0);
}

static void capacity_is_two_to_the_power_of_the_code(void)
{
    const struct {
        uint8_t code;
        uint32_t bytes;
    } cases[] = {
        {0, 1}, {0x10, UINT32_C(65536)}, {0x17, UINT32_C(8388608)}, {0x1F, UINT32_C(0x80000000)}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct spi_flash_jedec_id id = {0xEF, 0x40, cases[i].code};
        uint32_t bytes = 0;
        assert(spi_flash_capacity_bytes(&id, &bytes) == CRUX_OK);
        assert(bytes == cases[i].bytes);
    }
}

static void capacity_rejects_codes_that_do_not_fit_and_null_arguments(void)
{
    const struct spi_flash_jedec_id big = {0xEF, 0x40, 0x20};
    const struct spi_flash_jedec_id ok = {0xEF, 0x40, 0x17};
    uint32_t bytes = 123;
    assert(spi_flash_capacity_bytes(&big, &bytes) == CRUX_ERR_OUT_OF_RANGE);
    assert(bytes == 123);
    assert(spi_flash_capacity_bytes(NULL, &bytes) == CRUX_ERR_INVALID_ARGUMENT);
    assert(spi_flash_capacity_bytes(&ok, NULL) == CRUX_ERR_INVALID_ARGUMENT);
}

int main(void)
{
    one_exchange_sends_the_command_and_decodes_the_response();
    the_byte_clocked_during_the_command_is_ignored();
    an_absent_or_idle_device_is_an_io_error_and_leaves_the_output_alone();
    a_partly_idle_response_is_still_an_identification();
    provider_statuses_are_returned_unchanged_and_output_is_untouched();
    a_null_output_never_reaches_the_provider();
    capacity_is_two_to_the_power_of_the_code();
    capacity_rejects_codes_that_do_not_fit_and_null_arguments();
    return 0;
}
