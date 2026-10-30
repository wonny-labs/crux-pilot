#include "controller.h"
#include "runtime.h"
#include <crux/logging.h>
#include <stdio.h>
#include <string.h>
#if defined(CONFIG_LOG_BACKEND_UART)
#include <zephyr/drivers/serial/uart_emul.h>
#endif
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#if defined(CONFIG_LOG_BACKEND_NET)
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_net.h>
#endif
#include <zephyr/tc_util.h>
#include <zephyr/ztest.h>
#if defined(CONFIG_LOG_BACKEND_NET)
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/socket.h>
#endif

#if defined(CONFIG_LOG_BACKEND_UART)
static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(logging_test));
#endif
static struct k_mutex capture_lock;
#if defined(CONFIG_LOG_BACKEND_UART)
static char uart_capture[16384];
static size_t uart_used;
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
static char net_capture[16384];
static size_t net_used;
#endif
static struct k_thread *worker;
static void discover(const struct k_thread *thread, void *unused)
{
    (void)unused;
    const char *name = k_thread_name_get((k_tid_t)thread);
    if (name != NULL && strcmp(name, "logging") == 0)
        worker = (struct k_thread *)thread;
}
#if defined(CONFIG_LOG_BACKEND_UART)
static void collect(const struct device *dev, size_t size, void *unused)
{
    (void)size;
    (void)unused;
    uint8_t bytes[128];
    uint32_t count;
    k_mutex_lock(&capture_lock, K_FOREVER);
    while ((count = uart_emul_get_tx_data(dev, bytes, sizeof(bytes))) != 0) {
        zassert_true(uart_used + count < sizeof(uart_capture), "UART capture overflow");
        memcpy(uart_capture + uart_used, bytes, count);
        uart_used += count;
        uart_capture[uart_used] = '\0';
    }
    k_mutex_unlock(&capture_lock);
}
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
static int receiver_socket;
K_SEM_DEFINE(net_connected, 0, 1);
K_SEM_DEFINE(net_disconnected, 0, 1);
static struct net_mgmt_event_callback net_event_callback;
K_THREAD_STACK_DEFINE(receiver_stack, 1536);
static struct k_thread receiver_thread;
static int create_collector(void)
{
    int sock = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    if (sock < 0) {
        return sock;
    }
    struct net_sockaddr_in address = {
        .sin_family = NET_AF_INET,
        .sin_port = net_htons(15141),
        .sin_addr = NET_INADDR_ANY_INIT,
    };
    if (zsock_bind(sock, (struct net_sockaddr *)&address, sizeof(address)) != 0) {
        zsock_close(sock);
        return -1;
    }
    return sock;
}
static void network_event(struct net_mgmt_event_callback *callback, uint64_t event,
                          struct net_if *iface)
{
    (void)callback;
    (void)iface;
    if (event == NET_EVENT_L4_CONNECTED) {
        k_sem_give(&net_connected);
    } else if (event == NET_EVENT_L4_DISCONNECTED) {
        k_sem_give(&net_disconnected);
    }
}
static void drain_event_sem(struct k_sem *sem)
{
    while (k_sem_take(sem, K_NO_WAIT) == 0) {
    }
}
static void receive_syslog(void *unused1, void *unused2, void *unused3)
{
    (void)unused1;
    (void)unused2;
    (void)unused3;
    char datagram[CONFIG_LOG_BACKEND_NET_MAX_BUF_SIZE + 1];
    for (;;) {
        if (receiver_socket < 0) {
            k_sleep(K_MSEC(10));
            continue;
        }
        int sock = receiver_socket;
        ssize_t count = zsock_recv(sock, datagram, sizeof(datagram) - 1, 0);
        if (count <= 0) {
            continue;
        }
        k_mutex_lock(&capture_lock, K_FOREVER);
        if (net_used + (size_t)count + 1 < sizeof(net_capture)) {
            memcpy(net_capture + net_used, datagram, (size_t)count);
            net_used += (size_t)count;
            net_capture[net_used++] = '\n';
            net_capture[net_used] = '\0';
        }
        k_mutex_unlock(&capture_lock);
    }
}
#endif
static bool contains(const char *text)
{
    k_mutex_lock(&capture_lock, K_FOREVER);
    bool found = false;
#if defined(CONFIG_LOG_BACKEND_UART)
    found = strstr(uart_capture, text) != NULL;
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
    found = found || strstr(net_capture, text) != NULL;
#endif
    k_mutex_unlock(&capture_lock);
    return found;
}
#if defined(CONFIG_LOG_BACKEND_UART)
static bool contains_uart(const char *text)
{
    k_mutex_lock(&capture_lock, K_FOREVER);
    bool found = strstr(uart_capture, text) != NULL;
    k_mutex_unlock(&capture_lock);
    return found;
}
#if defined(CONFIG_LOG_BACKEND_NET)
static unsigned uart_records_containing(const char *text)
{
    unsigned count = 0;
    k_mutex_lock(&capture_lock, K_FOREVER);
    const char *record = uart_capture;
    const char *end;
    while ((end = strchr(record, '\n')) != NULL) {
        const char *match = strstr(record, text);
        if (match != NULL && match < end) {
            ++count;
        }
        record = end + 1;
    }
    k_mutex_unlock(&capture_lock);
    return count;
}
#endif
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
static bool contains_net(const char *text)
{
    k_mutex_lock(&capture_lock, K_FOREVER);
    bool found = strstr(net_capture, text) != NULL;
    k_mutex_unlock(&capture_lock);
    return found;
}
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
static unsigned net_records_containing(const char *text)
{
    unsigned count = 0;
    k_mutex_lock(&capture_lock, K_FOREVER);
    const char *record = net_capture;
    const char *end;
    while ((end = strchr(record, '\n')) != NULL) {
        const char *match = strstr(record, text);
        if (match != NULL && match < end) {
            ++count;
        }
        record = end + 1;
    }
    k_mutex_unlock(&capture_lock);
    return count;
}
static bool net_record_has_priority(const char *text, const char *priority)
{
    bool found = false;
    size_t priority_length = strlen(priority);
    k_mutex_lock(&capture_lock, K_FOREVER);
    const char *record = net_capture;
    const char *end;
    while ((end = strchr(record, '\n')) != NULL) {
        const char *match = strstr(record, text);
        if (match != NULL && match < end && (size_t)(end - record) >= priority_length &&
            strncmp(record, priority, priority_length) == 0) {
            found = true;
            break;
        }
        record = end + 1;
    }
    k_mutex_unlock(&capture_lock);
    return found;
}
#endif
static void wait_for(const char *text)
{
    for (unsigned i = 0; i < 200 && !contains(text); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_true(contains(text), "missing actual UART output: %s", text);
}
#if defined(CONFIG_LOG_BACKEND_UART)
static void wait_for_uart(const char *text)
{
    for (unsigned i = 0; i < 200 && !contains_uart(text); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_true(contains_uart(text), "missing actual UART output: %s", text);
}
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
static void wait_for_net(const char *text)
{
    for (unsigned i = 0; i < 200 && !contains_net(text); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_true(contains_net(text), "missing actual UDP Syslog output: %s", text);
}
#endif
static void *setup(void)
{
    k_mutex_init(&capture_lock);
#if defined(CONFIG_LOG_BACKEND_UART)
    zassert_true(device_is_ready(uart));
    uart_emul_callback_tx_data_ready_set(uart, collect, NULL);
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
    receiver_socket = create_collector();
    zassert_true(receiver_socket >= 0, "cannot create Syslog collector socket");
    k_thread_create(&receiver_thread, receiver_stack, K_THREAD_STACK_SIZEOF(receiver_stack),
                    receive_syslog, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    net_mgmt_init_event_callback(&net_event_callback, network_event,
                                 NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);
    net_mgmt_add_event_callback(&net_event_callback);
    struct net_if *iface = net_if_get_default();
    if (!net_if_is_up(iface)) {
        zassert_equal(net_if_up(iface), 0, "cannot bring up native_sim loopback interface");
    }
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", "before-l4-ready"), CRUX_OK);
    k_sleep(K_MSEC(100));
    zassert_false(contains_net("before-l4-ready"), "UDP backend started before L4 readiness");
    /* native_sim loopback has no connection-manager route events; inject the
     * Zephyr management event to exercise the native NET backend's listener. */
    net_mgmt_event_notify(NET_EVENT_L4_CONNECTED, iface);
    zassert_equal(k_sem_take(&net_connected, K_SECONDS(3)), 0,
                  "simulated L4-ready event was not delivered");
#endif
    k_thread_foreach(discover, NULL);
    zassert_not_null(worker);
    return NULL;
}
#if defined(CONFIG_LOG_BACKEND_UART)
ZTEST(logging, test_native_copy_filter_and_loss)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_DEBUG), CRUX_OK);
    k_thread_suspend(worker);
    char tag[] = "mutable";
    char text[] = "original 100% %s";
    zassert_equal(crux_log_write(CRUX_LOG_INFO, tag, text), CRUX_OK);
    memset(tag, 'x', sizeof(tag) - 1);
    memset(text, 'x', sizeof(text) - 1);
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "levels", "error-record"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_WARN, "levels", "warn-record"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_DEBUG, "levels", "debug-record"), CRUX_OK);
    zassert_equal(crux_log_set_level(CRUX_LOG_OFF), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "levels", "hidden-record"), CRUX_OK);
    zassert_equal(crux_log_set_level(CRUX_LOG_DEBUG), CRUX_OK);
    for (unsigned i = 0; i < 100; ++i)
        zassert_equal(crux_log_write(CRUX_LOG_INFO, "burst", "overflow payload retaining oldest"),
                      CRUX_OK);
    k_thread_resume(worker);
    wait_for("<inf> crux_logging: mutable: original 100% %s");
    wait_for("<err> crux_logging: levels: error-record");
    wait_for("<wrn> crux_logging: levels: warn-record");
    wait_for("<dbg> crux_logging: crux_log_impl_write: levels: debug-record");
    wait_for("messages dropped");
    zassert_false(contains("hidden-record"));
    zassert_false(contains("xxxxxxxx"));
}
#endif

ZTEST(logging, test_selected_backend_delivery)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_DEBUG), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "profile", "selected-error"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_WARN, "profile", "selected-warn"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "profile", "selected-info"), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_DEBUG, "profile", "selected-debug"), CRUX_OK);
#if defined(CONFIG_LOG_BACKEND_UART)
    wait_for_uart("<err> crux_logging: profile: selected-error");
    wait_for_uart("<wrn> crux_logging: profile: selected-warn");
    wait_for_uart("<inf> crux_logging: profile: selected-info");
    wait_for_uart("<dbg> crux_logging: crux_log_impl_write: profile: selected-debug");
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
    wait_for_net("profile: selected-error");
    wait_for_net("profile: selected-warn");
    wait_for_net("profile: selected-info");
    wait_for_net("profile: selected-debug");
    zassert_equal(net_records_containing("selected-error"), 1,
                  "each Syslog datagram must contain exactly one selected record");
    zassert_equal(net_records_containing("selected-warn"), 1,
                  "each Syslog datagram must contain exactly one selected record");
    zassert_equal(net_records_containing("selected-info"), 1,
                  "each Syslog datagram must contain exactly one selected record");
    zassert_equal(net_records_containing("selected-debug"), 1,
                  "each Syslog datagram must contain exactly one selected record");
    zassert_true(net_record_has_priority("selected-error", "<131>1 "),
                 "Syslog ERROR record must use local0.error priority 131");
    zassert_true(net_record_has_priority("selected-warn", "<132>1 "),
                 "Syslog WARN record must use local0.warning priority 132");
    zassert_true(net_record_has_priority("selected-info", "<134>1 "),
                 "Syslog INFO record must use local0.info priority 134");
    zassert_true(net_record_has_priority("selected-debug", "<135>1 "),
                 "Syslog DEBUG record must use local0.debug priority 135");
#endif
}

#if defined(CONFIG_LOG_BACKEND_NET)
ZTEST(logging, test_simulated_udp_send_failure_and_recovery)
{
    struct net_if *iface = net_if_get_default();
    const struct log_backend *backend = log_backend_net_get();
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", "before-send-failure"), CRUX_OK);
    wait_for_net("network: before-send-failure");
    zassert_true(log_backend_is_active(backend), "NET backend must be active before link loss");

    drain_event_sem(&net_connected);
    drain_event_sem(&net_disconnected);
    zassert_equal(net_if_down(iface), 0);
    int disconnected = k_sem_take(&net_disconnected, K_SECONDS(1));
    if (disconnected != 0) {
        /* Some native_sim configurations do not report the route transition. */
        net_mgmt_event_notify(NET_EVENT_L4_DISCONNECTED, iface);
        zassert_equal(k_sem_take(&net_disconnected, K_SECONDS(1)), 0,
                      "simulated L4 disconnect event was not delivered");
    }
    for (unsigned i = 0; i < 100 && log_backend_is_active(backend); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_false(log_backend_is_active(backend),
                  "NET backend must deactivate after route loss is reported at L4");

    int probe = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
    zassert_true(probe >= 0, "cannot create UDP route-failure probe");
    struct net_sockaddr_in destination = {
        .sin_family = NET_AF_INET,
        .sin_port = net_htons(15141),
        .sin_addr = {.s4_addr = {127, 0, 0, 1}},
    };
    for (unsigned i = 0; i < 16; ++i) {
        ssize_t probe_result =
            zsock_sendto(probe, "route-probe", sizeof("route-probe") - 1, 0,
                         (struct net_sockaddr *)&destination, sizeof(destination));
        zassert_true(probe_result < 0,
                     "native_sim must reject UDP transmission while its interface is down");
    }
    zassert_equal(zsock_close(probe), 0);

    /* Re-enable the backend while the route remains down so this burst checks
     * the active NET output path. This is injected L4 event-handler testing,
     * not a physical connection-manager validation. */
    drain_event_sem(&net_connected);
    net_mgmt_event_notify(NET_EVENT_L4_CONNECTED, iface);
    zassert_equal(k_sem_take(&net_connected, K_SECONDS(1)), 0,
                  "simulated L4 reconnect event was not delivered");
    for (unsigned i = 0; i < 100 && !log_backend_is_active(backend); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_true(log_backend_is_active(backend),
                 "NET backend must reactivate for the active send-failure burst");

    uint32_t active_max_call_ms = 0;
#if defined(CONFIG_LOG_BACKEND_UART)
    uint32_t active_last_submit_ms = 0;
#endif
    for (unsigned i = 0; i < 16; ++i) {
        char record[48];
        snprintk(record, sizeof(record), "active-route-down-%u", i);
        uint32_t start = k_uptime_get_32();
        zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", record), CRUX_OK);
        uint32_t elapsed = k_uptime_get_32() - start;
        if (elapsed > active_max_call_ms) {
            active_max_call_ms = elapsed;
        }
#if defined(CONFIG_LOG_BACKEND_UART)
        active_last_submit_ms = k_uptime_get_32();
#endif
    }
#if defined(CONFIG_LOG_BACKEND_UART)
    wait_for_uart("network: active-route-down-15");
    uint32_t active_uart_drain_ms = k_uptime_get_32() - active_last_submit_ms;
    zassert_equal(uart_records_containing("network: active-route-down-"), 16,
                  "UART must retain the full burst while NET is active without a route");
    TC_PRINT(
        "active NET route-down burst: max submit %u ms; last UART record observed after %u ms\n",
        active_max_call_ms, active_uart_drain_ms);
#else
    k_sleep(K_MSEC(100));
    TC_PRINT(
        "active NET route-down burst: max submit %u ms; UDP-only profile has no UART observation\n",
        active_max_call_ms);
#endif
    zassert_equal(net_records_containing("network: active-route-down-"), 0,
                  "active NET backend must not deliver route-down records to the collector");

    /* Test the NET backend's event handler explicitly. This is simulated
     * lifecycle delivery, not physical connection-manager validation. */
    drain_event_sem(&net_disconnected);
    net_mgmt_event_notify(NET_EVENT_L4_DISCONNECTED, iface);
    zassert_equal(k_sem_take(&net_disconnected, K_SECONDS(1)), 0);
    for (unsigned i = 0; i < 100 && log_backend_is_active(backend); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_false(log_backend_is_active(backend),
                  "NET backend must deactivate after simulated L4 disconnect");

    uint32_t max_call_ms = 0;
#if defined(CONFIG_LOG_BACKEND_UART)
    uint32_t last_submit_ms = 0;
#endif
    for (unsigned i = 0; i < 16; ++i) {
        char record[48];
        snprintk(record, sizeof(record), "inactive-route-down-%u", i);
        uint32_t start = k_uptime_get_32();
        zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", record), CRUX_OK);
        uint32_t elapsed = k_uptime_get_32() - start;
        if (elapsed > max_call_ms) {
            max_call_ms = elapsed;
        }
#if defined(CONFIG_LOG_BACKEND_UART)
        last_submit_ms = k_uptime_get_32();
#endif
    }
#if defined(CONFIG_LOG_BACKEND_UART)
    wait_for_uart("network: inactive-route-down-15");
    zassert_equal(uart_records_containing("network: inactive-route-down-"), 16,
                  "UART must retain the full burst while NET is deactivated");
    uint32_t uart_drain_ms = k_uptime_get_32() - last_submit_ms;
    TC_PRINT(
        "inactive NET route-down burst: max submit %u ms; last UART record observed after %u ms\n",
        max_call_ms, uart_drain_ms);
#else
    k_sleep(K_MSEC(100));
    TC_PRINT("inactive NET route-down burst: max submit %u ms; UDP-only profile has no UART "
             "observation\n",
             max_call_ms);
#endif
    zassert_equal(net_records_containing("network: inactive-route-down-"), 0,
                  "deactivated NET backend must not deliver the route-down log burst");

    zassert_equal(net_if_up(iface), 0);
    net_mgmt_event_notify(NET_EVENT_L4_CONNECTED, iface);
    zassert_equal(k_sem_take(&net_connected, K_SECONDS(1)), 0);
    for (unsigned i = 0; i < 100 && !log_backend_is_active(backend); ++i) {
        k_sleep(K_MSEC(10));
    }
    zassert_true(log_backend_is_active(backend), "NET backend unexpectedly deactivated");
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", "after-send-failure-recovery"), CRUX_OK);
    wait_for_net("network: after-send-failure-recovery");
}

ZTEST(logging, test_simulated_l4_disconnect_and_reactivation)
{
    struct net_if *iface = net_if_get_default();
    drain_event_sem(&net_connected);
    drain_event_sem(&net_disconnected);
    zassert_equal(net_if_down(iface), 0);
    /* Explicitly exercise Zephyr's L4 event handler; this is simulated event
     * delivery and does not validate a physical connection manager. */
    net_mgmt_event_notify(NET_EVENT_L4_DISCONNECTED, iface);
    zassert_equal(k_sem_take(&net_disconnected, K_SECONDS(1)), 0);
    uint32_t max_call_ms = 0;
#if defined(CONFIG_LOG_BACKEND_UART)
    uint32_t last_submit_ms = 0;
#endif
    for (unsigned i = 0; i < 16; ++i) {
        char record[40];
        snprintk(record, sizeof(record), "link-down-%u", i);
        uint32_t start = k_uptime_get_32();
        zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", record), CRUX_OK);
        uint32_t elapsed = k_uptime_get_32() - start;
        if (elapsed > max_call_ms) {
            max_call_ms = elapsed;
        }
#if defined(CONFIG_LOG_BACKEND_UART)
        last_submit_ms = k_uptime_get_32();
#endif
    }
#if defined(CONFIG_LOG_BACKEND_UART)
    wait_for_uart("network: link-down-15");
    TC_PRINT("simulated L4-down burst: max submit %u ms; last UART record observed after %u ms\n",
             max_call_ms, k_uptime_get_32() - last_submit_ms);
#else
    TC_PRINT(
        "simulated L4-down burst: max submit %u ms; UDP-only profile has no UART observation\n",
        max_call_ms);
#endif
    k_sleep(K_MSEC(100));
    zassert_equal(net_records_containing("link-down-"), 0,
                  "UDP backend emitted while the simulated L4 connection was down");

    zassert_equal(net_if_up(iface), 0);
    net_mgmt_event_notify(NET_EVENT_L4_CONNECTED, iface);
    zassert_equal(k_sem_take(&net_connected, K_SECONDS(1)), 0);
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", "after-network-recovery"), CRUX_OK);
    wait_for_net("network: after-network-recovery");
}

ZTEST(logging, test_no_collector_best_effort_without_ack)
{
    int previous_socket = receiver_socket;
    receiver_socket = -1;
    zassert_equal(zsock_close(previous_socket), 0);
    k_sleep(K_MSEC(20));

    uint32_t max_call_ms = 0;
#if defined(CONFIG_LOG_BACKEND_UART)
    uint32_t last_submit_ms = 0;
#endif
    for (unsigned i = 0; i < 16; ++i) {
        char record[40];
        snprintk(record, sizeof(record), "collector-absent-%u", i);
        uint32_t start = k_uptime_get_32();
        zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", record), CRUX_OK);
        uint32_t elapsed = k_uptime_get_32() - start;
        if (elapsed > max_call_ms) {
            max_call_ms = elapsed;
        }
#if defined(CONFIG_LOG_BACKEND_UART)
        last_submit_ms = k_uptime_get_32();
#endif
    }
#if defined(CONFIG_LOG_BACKEND_UART)
    wait_for_uart("network: collector-absent-15");
    TC_PRINT("no-listener burst: max submit %u ms; last UART record observed after %u ms\n",
             max_call_ms, k_uptime_get_32() - last_submit_ms);
#else
    TC_PRINT("no-listener burst: max submit %u ms; UDP-only profile has no UART observation\n",
             max_call_ms);
#endif

    receiver_socket = create_collector();
    zassert_true(receiver_socket >= 0, "cannot restore test Syslog collector");
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "network", "collector-restored"), CRUX_OK);
    wait_for_net("network: collector-restored");
}
#endif
ZTEST_SUITE(logging, NULL, setup, NULL, NULL, NULL);

static void producer(void *index, void *unused1, void *unused2)
{
    (void)unused1;
    (void)unused2;
    const char *tag = (const char *)index;
    for (unsigned i = 0; i < 3; ++i) {
        char text[16];
        int length = snprintf(text, sizeof(text), "record=%u", i);
        zassert_true(length > 0 && (size_t)length < sizeof(text));
        zassert_equal(crux_log_write(CRUX_LOG_INFO, tag, text), CRUX_OK);
    }
}
/* Observe only fully delimited native output, checking each producer's
 * sequence without imposing a total order across concurrent producers. */
static unsigned producer_records_in_capture(const char *capture, const char *tag)
{
    unsigned count = 0;
    const char *line = capture;
    const char *end;
    while ((end = strchr(line, '\n')) != NULL) {
        const char *match = strstr(line, tag);
        if (match != NULL && match < end) {
            char expected[64];
            int length = snprintf(expected, sizeof(expected), "%s: record=%u", tag, count);
            zassert_true(length > 0 && (size_t)length < sizeof(expected));
            zassert_true(match + length <= end && strncmp(match, expected, (size_t)length) == 0,
                         "corrupt, duplicate or reordered record");
            ++count;
        }
        line = end + 1;
    }
    return count;
}
#if defined(CONFIG_LOG_BACKEND_UART)
static unsigned uart_producer_records(const char *tag)
{
    k_mutex_lock(&capture_lock, K_FOREVER);
    unsigned count = producer_records_in_capture(uart_capture, tag);
    k_mutex_unlock(&capture_lock);
    return count;
}
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
static unsigned net_producer_records(const char *tag)
{
    k_mutex_lock(&capture_lock, K_FOREVER);
    unsigned count = producer_records_in_capture(net_capture, tag);
    k_mutex_unlock(&capture_lock);
    return count;
}
#endif
K_THREAD_STACK_ARRAY_DEFINE(producer_stacks, 3, 2048);
static struct k_thread producers[3];
ZTEST(logging, test_concurrent_native_records)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    const char *tags[] = {"producer.a", "producer.b", "producer.c"};
    for (unsigned i = 0; i < 3; ++i)
        k_thread_create(&producers[i], producer_stacks[i],
                        K_THREAD_STACK_SIZEOF(producer_stacks[i]), producer, (void *)tags[i], NULL,
                        NULL, 5, 0, K_NO_WAIT);
    for (unsigned i = 0; i < 3; ++i)
        zassert_equal(k_thread_join(&producers[i], K_SECONDS(2)), 0);
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned attempt = 0; attempt < 200; ++attempt) {
            bool complete = true;
#if defined(CONFIG_LOG_BACKEND_UART)
            complete = complete && uart_producer_records(tags[i]) == 3;
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
            complete = complete && net_producer_records(tags[i]) == 3;
#endif
            if (complete) {
                break;
            }
            k_sleep(K_MSEC(10));
        }
#if defined(CONFIG_LOG_BACKEND_UART)
        zassert_equal(uart_producer_records(tags[i]), 3,
                      "missing complete concurrent UART records");
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
        zassert_equal(net_producer_records(tags[i]), 3, "missing complete concurrent UDP records");
#endif
    }
}

ZTEST(logging, test_maximum_text_not_truncated)
{
    char text[CRUX_LOG_TEXT_MAX + 1];
    memset(text, 'q', CRUX_LOG_TEXT_MAX);
    memcpy(text + CRUX_LOG_TEXT_MAX - 8, "END-256!", 8);
    text[CRUX_LOG_TEXT_MAX] = '\0';
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    zassert_equal(crux_log_write(CRUX_LOG_INFO, "maximum", text), CRUX_OK);
#if defined(CONFIG_LOG_BACKEND_UART)
    wait_for_uart(text);
#endif
#if defined(CONFIG_LOG_BACKEND_NET)
    wait_for_net(text);
#endif
}

ZTEST(logging, test_portable_consumers)
{
    zassert_equal(crux_log_set_level(CRUX_LOG_INFO), CRUX_OK);
    zassert_equal(logging_runtime_record(42, 0), CRUX_OK);
    zassert_equal(logging_cpp_record(43, -7), CRUX_OK);
    zassert_equal(logging_controller_state(false), CRUX_OK);
    wait_for("runtime.c: sequence=42 code=0");
    wait_for("runtime.cpp: sequence=43 code=-7");
    wait_for("controller: fault");
}
static void in_interrupt(const void *unused)
{
    (void)unused;
    zassert_equal(crux_log_write(CRUX_LOG_ERROR, "isr", "forbidden"), CRUX_ERR_NOT_SUPPORTED);
    zassert_equal(crux_log_set_level(CRUX_LOG_OFF), CRUX_ERR_NOT_SUPPORTED);
}
ZTEST(logging, test_isr_rejected)
{
    irq_offload(in_interrupt, NULL);
}
