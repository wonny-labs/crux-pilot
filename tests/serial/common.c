#include "serial_port.h"
#include <assert.h>
#include <crux/serial.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Small native-result boundary, not a replacement UART/OS. Tests exercise the
 * real public lifecycle/validation code against explicit native outcomes. */
static crux_status_t open_result, close_result, transfer_result;
static bool metadata_busy, nested, nested_done;
static size_t progress = 3;
static crux_serial_t live;
static const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
bool crux_serial_context_allowed(void)
{
    return true;
}
crux_status_t crux_serial_monotonic_now_ms(uint64_t *now)
{
    *now = 100;
    return CRUX_OK;
}
crux_status_t serial_port_metadata_lock(const serial_deadline_t *deadline)
{
    if (metadata_busy && deadline != NULL)
        return deadline->poll ? CRUX_ERR_BUSY : CRUX_ERR_TIMEOUT;
    return CRUX_OK;
}
void serial_port_metadata_unlock(void)
{
}
crux_status_t crux_serial_impl_open(uint32_t port, const crux_serial_config_t *cfg)
{
    (void)port;
    (void)cfg;
    return open_result;
}
crux_status_t crux_serial_impl_close(uint32_t port)
{
    (void)port;
    return close_result;
}
crux_status_t crux_serial_impl_read(uint32_t port, void *data, size_t size,
                               const serial_deadline_t *deadline, size_t *count)
{
    (void)port;
    (void)deadline;
    if (nested && !nested_done) {
        nested_done = true;
        crux_serial_t alias = live;
        size_t n = 99;
        uint8_t byte = 0;
        assert(crux_serial_close(&alias) == CRUX_ERR_BUSY);
        assert(alias.generation == live.generation);
        assert(crux_serial_read(live, &byte, 1, 0, &n) == CRUX_ERR_BUSY && n == 0);
        assert(crux_serial_write(live, &byte, 1, 0, &n) == CRUX_OK && n == 1);
    }
    if (transfer_result != CRUX_OK)
        return transfer_result;
    *count = size < progress ? size : progress;
    memcpy(data, "abc", *count);
    return CRUX_OK;
}
crux_status_t crux_serial_impl_write(uint32_t port, const void *data, size_t size,
                                const serial_deadline_t *deadline, size_t *count)
{
    (void)port;
    (void)data;
    (void)deadline;
    if (transfer_result != CRUX_OK)
        return transfer_result;
    *count = size < progress ? size : progress;
    return CRUX_OK;
}

static void validation(void)
{
    crux_serial_t out = {0};
    assert(crux_serial_open(0, NULL, &out) == CRUX_ERR_INVALID_ARGUMENT);
    assert(out.slot == 0 && out.generation == 0);
    assert(crux_serial_open(0, &config, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    crux_serial_config_t bad = config;
    bad.baud_rate = 0;
    assert(crux_serial_open(0, &bad, &out) == CRUX_ERR_INVALID_ARGUMENT);
    bad.baud_rate = 12345;
    assert(crux_serial_open(0, &bad, &out) == CRUX_ERR_NOT_SUPPORTED);
    bad = config;
    bad.data_bits = 9;
    assert(crux_serial_open(0, &bad, &out) == CRUX_ERR_INVALID_ARGUMENT);
    bad = config;
    bad.stop_bits = 0;
    assert(crux_serial_open(0, &bad, &out) == CRUX_ERR_INVALID_ARGUMENT);
    bad = config;
    // Deliberately invalid enum representation tests public argument validation.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    bad.parity = (crux_serial_parity_t)99;
    assert(crux_serial_open(0, &bad, &out) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_serial_open(UINT32_MAX, &config, &out) == CRUX_ERR_INVALID_ARGUMENT);
    open_result = CRUX_ERR_NOT_READY;
    assert(crux_serial_open(0, &config, &out) == CRUX_ERR_NOT_READY && out.slot == 0);
    open_result = CRUX_OK;
    assert(crux_serial_close(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_serial_close(&out) == CRUX_ERR_INVALID_STATE);
}
static void lifetime(void)
{
    assert(crux_serial_open(0, &config, &live) == CRUX_OK);
    crux_serial_t second = {0};
    assert(crux_serial_open(0, &config, &second) == CRUX_ERR_BUSY && second.slot == 0);
    nested = true;
    uint8_t bytes[4] = {0};
    size_t n = 0;
    assert(crux_serial_read(live, bytes, sizeof(bytes), 1, &n) == CRUX_OK && n == 3);
    assert(nested_done);
    crux_serial_t stale = live;
    assert(crux_serial_close(&live) == CRUX_OK && live.slot == 0 && live.generation == 0);
    assert(crux_serial_open(0, &config, &live) == CRUX_OK);
    assert(crux_serial_read(stale, bytes, sizeof(bytes), 0, &n) == CRUX_ERR_INVALID_STATE);
    close_result = CRUX_ERR_IO;
    assert(crux_serial_close(&live) == CRUX_ERR_IO && live.slot == 0);
    assert(crux_serial_close(&stale) == CRUX_ERR_INVALID_STATE);
}
static void transfers(void)
{
    assert(crux_serial_open(0, &config, &live) == CRUX_OK);
    uint8_t bytes[5] = {9, 9, 9, 9, 9};
    size_t n = 99;
    assert(crux_serial_read(live, bytes, 5, 1, &n) == CRUX_OK && n == 3);
    assert(memcmp(bytes, "abc", 3) == 0 && bytes[3] == 9 && bytes[4] == 9);
    assert(crux_serial_write(live, bytes, 5, 1, &n) == CRUX_OK && n == 3);
    transfer_result = CRUX_ERR_IO;
    memset(bytes, 9, sizeof(bytes));
    assert(crux_serial_read(live, bytes, 5, 1, &n) == CRUX_ERR_IO && n == 0);
    assert(bytes[0] == 9 && bytes[4] == 9);
    assert(crux_serial_write(live, bytes, 5, 1, &n) == CRUX_ERR_IO && n == 0);
    assert(crux_serial_read(live, NULL, 0, 0, &n) == CRUX_OK && n == 0);
    assert(crux_serial_write(live, NULL, 0, 0, &n) == CRUX_OK && n == 0);
    assert(crux_serial_read(live, NULL, 1, 0, &n) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_serial_write(live, bytes, 1, 0, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    assert(crux_serial_close(&live) == CRUX_OK);
}
static void timeout(void)
{
    assert(crux_serial_open(0, &config, &live) == CRUX_OK);
    uint8_t byte = 7;
    size_t n = 99;
    assert(crux_serial_read(live, &byte, 1, (uint32_t)INT32_MAX + 1, &n) == CRUX_ERR_OUT_OF_RANGE &&
           n == 0);
    metadata_busy = true;
    assert(crux_serial_read(live, &byte, 1, 0, &n) == CRUX_ERR_BUSY && n == 0 && byte == 7);
    assert(crux_serial_read(live, &byte, 1, 1, &n) == CRUX_ERR_TIMEOUT && n == 0 && byte == 7);
    metadata_busy = false;
    transfer_result = CRUX_ERR_TIMEOUT;
    assert(crux_serial_read(live, &byte, 1, 1, &n) == CRUX_ERR_TIMEOUT && n == 0 && byte == 7);
    assert(crux_serial_close(&live) == CRUX_OK);
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "validation"))
        validation();
    else if (!strcmp(argv[1], "lifetime"))
        lifetime();
    else if (!strcmp(argv[1], "transfers"))
        transfers();
    else if (!strcmp(argv[1], "timeout"))
        timeout();
    else
        assert(false);
    return 0;
}
