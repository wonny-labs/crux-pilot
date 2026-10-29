/*
 * Issue #103: real Zephyr native Logging dispatch to UART and IPv4 UDP.
 * Only native_sim loopback is exercised, not target Ethernet or Crux's
 * existing UART-only portable provider. No claim of peer delivery follows
 * from a LOG_* call or a successful socket send.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/arch/posix/posix_trace.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_backend_net.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(spike103_udp, LOG_LEVEL_INF);
#define UDP_PORT 15141U
#define UDP_MARKER "SPIKE103_UDP_AND_UART"
#define DOWN_MARKER "SPIKE103_UART_WHILE_NET_DOWN"

static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(spike103_udp_uart));
static struct k_mutex capture_lock;
static char uart_capture[2048];
static size_t uart_used;

/* Preserve the independent Twister console after native UART Console setup. */
static int spike_trace_char(int c)
{
    static char line[256];
    static size_t used;

    if (c == '\r') {
        return c;
    }
    line[used++] = (char)c;
    if (c == '\n' || used == sizeof(line) - 1) {
        line[used] = '\0';
        posix_print_trace("%s", line);
        used = 0;
    }
    return c;
}

static int spike_restore_console(void)
{
    __printk_hook_install(spike_trace_char);
    return 0;
}

SYS_INIT(spike_restore_console, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

static void on_tx(const struct device *dev, size_t size, void *context)
{
    ARG_UNUSED(size);
    ARG_UNUSED(context);
    uint8_t data[128];
    uint32_t count;

    k_mutex_lock(&capture_lock, K_FOREVER);
    while ((count = uart_emul_get_tx_data(dev, data, sizeof(data))) != 0) {
        if (uart_used + count < sizeof(uart_capture)) {
            memcpy(uart_capture + uart_used, data, count);
            uart_used += count;
            uart_capture[uart_used] = '\0';
        }
    }
    k_mutex_unlock(&capture_lock);
}

static bool uart_contains(const char *text)
{
    bool found;

    k_mutex_lock(&capture_lock, K_FOREVER);
    found = strstr(uart_capture, text) != NULL;
    k_mutex_unlock(&capture_lock);
    return found;
}

static bool uart_wait(const char *text)
{
    for (unsigned i = 0; i < 300; ++i) {
        if (uart_contains(text)) {
            return true;
        }
        k_sleep(K_MSEC(10));
    }
    return false;
}

static void *setup(void)
{
    k_mutex_init(&capture_lock);
    zassert_true(device_is_ready(uart));
    uart_emul_callback_tx_data_ready_set(uart, on_tx, NULL);
    return NULL;
}

ZTEST(spike103_udp, test_real_udp_syslog_and_uart_with_network_down)
{
    int receiver = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    zassert_true(receiver >= 0, "cannot create Zephyr UDP receive socket");

    struct net_sockaddr_in address = {
        .sin_family = NET_AF_INET,
        .sin_port = net_htons(UDP_PORT),
    };
    zassert_equal(net_addr_pton(NET_AF_INET, "127.0.0.1", &address.sin_addr), 0);
    zassert_equal(zsock_bind(receiver, (struct net_sockaddr *)&address, sizeof(address)), 0);

    /* Explicit start, after network/collector readiness. No autostart race. */
    zassert_true(log_backend_net_set_addr("127.0.0.1:15141"));
    log_backend_net_start();
    LOG_INF("%s", UDP_MARKER);

    struct zsock_pollfd pollfd = {.fd = receiver, .events = ZSOCK_POLLIN};
    int polled = zsock_poll(&pollfd, 1, 3000);
    char udp_bytes[640] = {0};
    ssize_t received = polled > 0 && (pollfd.revents & ZSOCK_POLLIN)
                           ? zsock_recv(receiver, udp_bytes, sizeof(udp_bytes) - 1, 0)
                           : -1;
    bool udp_ok = received > 0 && strstr(udp_bytes, UDP_MARKER) != NULL;
    bool uart_ok = uart_wait(UDP_MARKER);
    printk("SPIKE103_UDP_RESULT udp=%d uart=%d bytes=%d poll=%d\n", udp_ok, uart_ok, (int)received,
           polled);
    zassert_true(udp_ok, "native network Syslog not received over Zephyr loopback");
    zassert_true(uart_ok, "native UART output missing while network backend active");

    struct net_if *iface = net_if_get_default();
    zassert_not_null(iface);
    net_if_carrier_off(iface);
    LOG_INF("%s", DOWN_MARKER);
    bool uart_during_down = uart_wait(DOWN_MARKER);
    net_if_carrier_on(iface);
    printk("SPIKE103_NET_DOWN_RESULT uart=%d\n", uart_during_down);
    zassert_true(uart_during_down, "UART stalled after simulated interface loss");
    zassert_equal(zsock_close(receiver), 0);
}

ZTEST_SUITE(spike103_udp, NULL, setup, NULL, NULL, NULL);
