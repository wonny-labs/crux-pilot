#include "serial_zephyr.h"
#include <crux/serial.h>
#include <string.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
static const struct device *dev = DEVICE_DT_GET(DT_ALIAS(crux_serial0));
static const struct device *override_device;
const struct device *__real_serial_zephyr_device(unsigned port);
const struct device *__wrap_serial_zephyr_device(unsigned port) {
    return override_device ? override_device : __real_serial_zephyr_device(port);
}
static uart_irq_callback_user_data_t cached;
static void *cached_context;
static void capture_callback(const struct device *device, uart_irq_callback_user_data_t cb,
                             void *context) {
    if (cb) {
        cached = cb;
        cached_context = context;
    }
    const struct uart_driver_api *api = dev->api;
    api->irq_callback_set(device, cb, context);
}
static void late_callback(const void *unused) {
    ARG_UNUSED(unused);
    cached(dev, cached_context);
}
static const crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
static crux_serial_t open_port(void) {
    crux_serial_t handle;
    zassert_ok(crux_serial_open(0, &config, &handle));
    return handle;
}
static void before(void *unused) {
    ARG_UNUSED(unused);
    uart_emul_flush_rx_data(dev);
    uart_emul_flush_tx_data(dev);
    uart_emul_set_errors(dev, 0);
}
ZTEST(serial, test_binary_configuration) {
    crux_serial_t handle = open_port();
    struct uart_config actual;
    zassert_ok(uart_config_get(dev, &actual));
    zassert_equal(actual.baudrate, 115200);
    zassert_equal(actual.data_bits, UART_CFG_DATA_BITS_8);
    unsigned char input[] = {0, 0xff, '\n', 0x51}, output[4] = {0};
    zassert_equal(uart_emul_put_rx_data(dev, input, sizeof(input)), sizeof(input));
    size_t count;
    zassert_ok(crux_serial_read(handle, output, sizeof(output), 100, &count));
    zassert_equal(count, sizeof(input));
    zassert_mem_equal(input, output, count);
    zassert_ok(crux_serial_write(handle, input, sizeof(input), 100, &count));
    zassert_equal(count, sizeof(input));
    k_sleep(K_MSEC(3));
    zassert_equal(uart_emul_get_tx_data(dev, output, sizeof(output)), sizeof(input));
    zassert_mem_equal(input, output, sizeof(input));
    crux_serial_t stale = handle;
    zassert_ok(crux_serial_close(&handle));
    zassert_equal(crux_serial_read(stale, output, 1, 0, &count), CRUX_ERR_INVALID_STATE);
    crux_serial_config_t alternate = {9600, 7, 2, CRUX_SERIAL_PARITY_EVEN};
    zassert_ok(crux_serial_open(0, &alternate, &handle));
    zassert_ok(uart_config_get(dev, &actual));
    zassert_equal(actual.data_bits, UART_CFG_DATA_BITS_7);
    zassert_equal(actual.stop_bits, UART_CFG_STOP_BITS_2);
    zassert_equal(actual.parity, UART_CFG_PARITY_EVEN);
    zassert_ok(crux_serial_close(&handle));
}
ZTEST(serial, test_poll_timeout_backpressure) {
    crux_serial_t handle = open_port();
    unsigned char data[512] = {0x39};
    size_t count;
    zassert_equal(crux_serial_read(handle, data, 1, 0, &count), CRUX_ERR_BUSY);
    zassert_equal(crux_serial_read(handle, data, 1, 10, &count), CRUX_ERR_TIMEOUT);
    zassert_equal(data[0], 0x39);
    size_t accepted = 0;
    for (int i = 0; i < 10; ++i) {
        crux_status_t result = crux_serial_write(handle, data, sizeof(data), 0, &count);
        if (result == CRUX_ERR_BUSY)
            break;
        zassert_ok(result);
        accepted += count;
        k_sleep(K_MSEC(1));
    }
    zassert_equal(accepted, 264); /* provider 256 + emulated hardware FIFO 8 */
    zassert_equal(crux_serial_write(handle, data, 1, 10, &count), CRUX_ERR_TIMEOUT);
    zassert_equal(count, 0);
    zassert_ok(crux_serial_close(&handle));
}
ZTEST(serial, test_observed_error_and_overflow) {
    crux_serial_t handle = open_port();
    unsigned char data[512] = {0x51};
    size_t count;
    uart_emul_set_errors(dev, UART_ERROR_PARITY);
    zassert_equal(crux_serial_read(handle, data, 1, 0, &count), CRUX_ERR_IO);
    zassert_equal(crux_serial_write(handle, data, 1, 0, &count), CRUX_ERR_IO);
    zassert_ok(crux_serial_close(&handle));
    handle = open_port();
    zassert_equal(uart_emul_put_rx_data(dev, data, sizeof(data)), sizeof(data));
    k_sleep(K_MSEC(10));
    zassert_equal(crux_serial_read(handle, data, 1, 0, &count), CRUX_ERR_IO);
    zassert_equal(count, 0);
    zassert_ok(crux_serial_close(&handle));
}
ZTEST(serial, test_mapping_and_late_callback) {
    struct device proxy = *dev;
    struct device_state state = {0};
    struct uart_driver_api api = *(const struct uart_driver_api *)dev->api;
    proxy.api = &api;
    proxy.state = &state;
    override_device = &proxy;
    crux_serial_t handle;
    zassert_equal(crux_serial_open(0, &config, &handle), CRUX_ERR_NOT_READY);
    state.initialized = true;
    api.irq_callback_set = NULL;
    zassert_equal(crux_serial_open(0, &config, &handle), CRUX_ERR_NOT_SUPPORTED);
    /* Known console ownership is rejected before native state is changed. */
    override_device = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    zassert_equal(crux_serial_open(0, &config, &handle), CRUX_ERR_BUSY);
    override_device = &proxy;
    api.irq_callback_set = capture_callback;
    zassert_ok(crux_serial_open(0, &config, &handle));
    zassert_not_null(cached);
    crux_serial_t stale = handle;
    zassert_ok(crux_serial_close(&handle));
    /* Cached invocation in real ISR context after detach must be inert. */
    irq_offload(late_callback, NULL);
    zassert_ok(crux_serial_open(0, &config, &handle));
    unsigned char byte = 0x39, output = 0;
    size_t count;
    zassert_equal(crux_serial_read(stale, &output, 1, 0, &count), CRUX_ERR_INVALID_STATE);
    zassert_equal(uart_emul_put_rx_data(dev, &byte, 1), 1);
    irq_offload(late_callback, NULL);
    zassert_ok(crux_serial_read(handle, &output, 1, 100, &count));
    zassert_equal(output, byte);
    zassert_ok(crux_serial_close(&handle));
    irq_offload(late_callback, NULL);
    override_device = NULL;
}
ZTEST_SUITE(serial, NULL, NULL, before, NULL, NULL);
