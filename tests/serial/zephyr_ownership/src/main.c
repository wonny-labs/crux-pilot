#include <crux/serial.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/ztest.h>
ZTEST(serial_ownership, test_separate_logging_uart_reserved) {
    const struct device *dev = DEVICE_DT_GET(DT_ALIAS(crux_serial0));
    struct uart_config before, after;
    zassert_ok(uart_config_get(dev, &before));
    crux_serial_config_t requested = {9600, 7, 2, CRUX_SERIAL_PARITY_EVEN};
    crux_serial_t handle;
    crux_status_t result = crux_serial_open(0, &requested, &handle);
    if (result == CRUX_OK)
        (void)crux_serial_close(&handle);
    zassert_equal(result, CRUX_ERR_BUSY);
    zassert_ok(uart_config_get(dev, &after));
    zassert_equal(before.baudrate, after.baudrate);
    zassert_equal(before.data_bits, after.data_bits);
}
ZTEST_SUITE(serial_ownership, NULL, NULL, NULL, NULL, NULL);
