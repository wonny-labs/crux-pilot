#include "fake_serial.h"

#ifndef CRUX_CLI_TEST_COMPOSITION
#error "The fake Serial is a test composition, and must not reach a product image"
#endif

cli_fake_serial_t cli_fake_serial;

void cli_fake_serial_reset(void)
{
    cli_fake_reset(&cli_fake_serial.transport);
    cli_fake_serial.open_result = CRUX_OK;
    cli_fake_serial.close_result = CRUX_OK;
    cli_fake_serial.close_busy_count = 0;
    cli_fake_serial.read_end_status = CRUX_ERR_IO;
    cli_fake_serial.write_end_status = CRUX_ERR_IO;
    cli_fake_serial.read_wait_status = CRUX_ERR_TIMEOUT;
    cli_fake_serial.open_calls = 0;
    cli_fake_serial.close_calls = 0;
    cli_fake_serial.handles_open = 0;
    cli_fake_serial.last_port = 0;
    cli_fake_serial.on_open = NULL;
}

crux_status_t crux_serial_open(crux_serial_port_t port, const crux_serial_config_t *config,
                               crux_serial_t *out)
{
    if (cli_fake_serial.on_open != NULL) {
        cli_fake_serial.on_open();
    }
    cli_fake_serial.open_calls += 1;
    cli_fake_serial.last_port = port;
    cli_fake_serial.last_config = *config;
    if (cli_fake_serial.open_result != CRUX_OK) {
        return cli_fake_serial.open_result;
    }
    cli_fake_serial.handles_open += 1;
    out->slot = 1;
    out->generation = cli_fake_serial.open_calls;
    return CRUX_OK;
}

crux_status_t crux_serial_read(crux_serial_t serial, void *buffer, size_t capacity,
                               uint32_t timeout_ms, size_t *transferred)
{
    (void)serial;
    *transferred = 0;
    int result = cli_fake_read(&cli_fake_serial.transport, buffer, capacity, timeout_ms);
    if (result < 0) {
        return cli_fake_serial.read_end_status;
    }
    if (result == 0) {
        return cli_fake_serial.read_wait_status;
    }
    *transferred = (size_t)result;
    return CRUX_OK;
}

crux_status_t crux_serial_write(crux_serial_t serial, const void *buffer, size_t size,
                                uint32_t timeout_ms, size_t *transferred)
{
    (void)serial;
    *transferred = 0;
    int result = cli_fake_write(&cli_fake_serial.transport, buffer, size, timeout_ms);
    if (result < 0) {
        return cli_fake_serial.write_end_status;
    }
    if (result == 0) {
        return CRUX_ERR_TIMEOUT;
    }
    *transferred = (size_t)result;
    return CRUX_OK;
}

crux_status_t crux_serial_close(crux_serial_t *serial)
{
    cli_fake_serial.close_calls += 1;
    if (cli_fake_serial.close_busy_count > 0) {
        cli_fake_serial.close_busy_count -= 1;
        return CRUX_ERR_BUSY;
    }
    cli_fake_serial.handles_open -= 1;
    serial->slot = 0;
    serial->generation = 0;
    return cli_fake_serial.close_result;
}
