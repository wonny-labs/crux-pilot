#include "controller.h"
#include "runtime.h"
#include <stdio.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_ZTEST
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/ztest.h>
static const struct device *dev = DEVICE_DT_GET(DT_ALIAS(crux_serial0));
static unsigned char request[16];
static size_t accumulated, frame_size;
static void peer(const struct device *uart, size_t size, void *unused) {
    ARG_UNUSED(unused);
    unsigned char byte;
    while (size-- && uart_emul_get_tx_data(uart, &byte, 1) == 1) {
        request[accumulated++] = byte;
        if (accumulated == frame_size) {
            request[3] = 0x41;
            zassert_equal(uart_emul_put_rx_data(uart, request, frame_size), frame_size);
            accumulated = 0;
        }
    }
}
static void before(void *unused) {
    ARG_UNUSED(unused);
    accumulated = 0;
    frame_size = 16;
    uart_emul_flush_rx_data(dev);
    uart_emul_flush_tx_data(dev);
    uart_emul_callback_tx_data_ready_set(dev, peer, NULL);
}
ZTEST(serial_consumer, test_c17_and_cpp17) {
    crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
    serial_runtime_result_t result;
    zassert_ok(serial_runtime_transaction(0, &config, UINT64_C(0x19ff0039), 200, &result));
    zassert_ok(result.cleanup_status);
    zassert_ok(serial_cpp_transaction(0, &config, UINT64_C(0x5aff0019), 200, &result));
    zassert_ok(result.cleanup_status);
}
ZTEST(serial_consumer, test_controller) {
    frame_size = 8;
    zassert_ok(serial_controller_run(0, UINT32_C(0x12345678)));
}
ZTEST_SUITE(serial_consumer, NULL, NULL, before, NULL, NULL);
#else
/* Cross-build fixture only: none of this is evidence of physical UART behaviour. */
int main(void) {
    crux_serial_config_t config = {115200, 8, 1, CRUX_SERIAL_PARITY_NONE};
    serial_runtime_result_t result;
    crux_status_t c = serial_runtime_transaction(0, &config, UINT64_C(0x19ff0039), 200, &result);
    crux_status_t cpp = serial_cpp_transaction(0, &config, UINT64_C(0x5aff0019), 200, &result);
    crux_status_t controller = serial_controller_run(0, UINT32_C(0x12345678));
    printf("Serial fixture C=%d C++=%d controller=%d (peer acknowledgement required)\n", c, cpp,
           controller);
    return 0;
}
#endif
