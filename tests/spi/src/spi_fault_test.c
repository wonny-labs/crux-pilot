/*
 * Portable-layer contract tests against a test-only provider that records each call and can
 * inject failures. It replaces the shipped loopback provider, which always succeeds and cannot
 * exercise error paths. It is not linked into any product target.
 */
#include <crux/spi.h>

#include "spi_port.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

static struct {
    int calls;
    const uint8_t *tx_data;
    uint8_t *rx_data;
    size_t length;
    crux_status_t inject;
} fake;

crux_status_t crux_spi_impl_transceive(const uint8_t *tx_data, uint8_t *rx_data, size_t length)
{
    fake.calls++;
    fake.tx_data = tx_data;
    fake.rx_data = rx_data;
    fake.length = length;
    if (fake.inject != CRUX_OK) {
        return fake.inject;
    }
    for (size_t i = 0; i < length; ++i) {
        rx_data[i] = (uint8_t)~tx_data[i]; /* a peripheral that answers with the complement */
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
    const uint8_t tx[] = {0x10};
    uint8_t rx[1] = {0};

    reset();
    assert(crux_spi_transceive(NULL, rx, sizeof(rx)) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_spi_transceive(tx, NULL, sizeof(tx)) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_spi_transceive(tx, rx, 0) == CRUX_ERR_INVALID_ARGUMENT);
    assert(fake.calls == 0);
}

static void a_valid_request_reaches_the_provider_unchanged(void)
{
    const uint8_t tx[] = {0x10, 0x20, 0x30};
    uint8_t rx[sizeof(tx)] = {0};

    reset();
    assert(crux_spi_transceive(tx, rx, sizeof(tx)) == CRUX_OK);
    assert(fake.calls == 1);
    assert(fake.tx_data == tx && fake.rx_data == rx && fake.length == sizeof(tx));
    for (size_t i = 0; i < sizeof(tx); ++i) {
        assert(rx[i] == (uint8_t)~tx[i]);
    }
}

static void provider_failures_are_returned_unchanged(void)
{
    const crux_status_t injected[] = {CRUX_ERR_NOT_READY, CRUX_ERR_IO, CRUX_ERR_NOT_SUPPORTED};
    const uint8_t tx[] = {0x10};
    uint8_t rx[1] = {0};

    for (size_t i = 0; i < sizeof(injected) / sizeof(injected[0]); ++i) {
        reset();
        fake.inject = injected[i];
        assert(crux_spi_transceive(tx, rx, sizeof(tx)) == injected[i]);
        assert(fake.calls == 1);
    }
}

int main(void)
{
    invalid_requests_never_reach_the_provider();
    a_valid_request_reaches_the_provider_unchanged();
    provider_failures_are_returned_unchanged();
    return 0;
}
