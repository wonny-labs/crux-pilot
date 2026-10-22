#include "stack_scenario.h"
#include <crux/netif.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net_buf.h>
#include <zephyr/ztest.h>
static struct k_thread peer_thread;
K_THREAD_STACK_DEFINE(peer_stack, 4096);
static void (*peer_entry)(void *);
static void *peer_context;
static void run_peer(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    peer_entry(peer_context);
}
void networking_peer_start(void (*entry)(void *), void *context) {
    peer_entry = entry;
    peer_context = context;
    k_thread_create(&peer_thread, peer_stack, K_THREAD_STACK_SIZEOF(peer_stack), run_peer, NULL,
                    NULL, NULL, 5, 0, K_NO_WAIT);
}
int networking_peer_join(void) {
    return k_thread_join(&peer_thread, K_SECONDS(3));
}
static void used(struct net_context *context, void *count) {
    if (net_context_is_used(context))
        ++*(unsigned int *)count;
}
void networking_fixture_link(bool up) {
    struct net_if *iface = net_if_get_default();
    if (up)
        net_if_carrier_on(iface);
    else
        net_if_carrier_off(iface);
    k_sleep(K_MSEC(50));
    crux_netif_status_t status;
    zassert_equal(crux_netif_get_status(&status), CRUX_OK);
    zassert_equal(status.link, up ? CRUX_NETIF_LINK_UP : CRUX_NETIF_LINK_DOWN);
}
void networking_resources_active(void) {
    unsigned int count = 0;
    net_context_foreach(used, &count);
    printk("STACK_NATIVE_ACTIVE contexts=%u\n", count);
    zassert_true(count >= 2, "Standard calls must allocate actual Zephyr contexts");
}
ZTEST(networking, test_primary_interface_observation) {
    struct net_if *iface = net_if_get_default();
    zassert_not_null(iface);
    crux_netif_status_t status;
    zassert_equal(crux_netif_get_status(&status), CRUX_OK);
    zassert_true(status.administrative_up);
    zassert_true(status.ipv4_available);
    zassert_equal(status.ipv4_address[0], 127);
    zassert_equal(status.link, CRUX_NETIF_LINK_UP);
    net_if_carrier_off(iface);
    zassert_equal(crux_netif_get_status(&status), CRUX_OK);
    zassert_equal(status.link, CRUX_NETIF_LINK_DOWN);
    zassert_true(status.ipv4_available);
    net_if_carrier_on(iface);
    zassert_equal(crux_netif_get_status(&status), CRUX_OK);
    zassert_equal(status.link, CRUX_NETIF_LINK_UP);
}
int networking_resources_idle(void) {
    int64_t started = k_uptime_get();
    struct k_mem_slab *rx, *tx;
    struct net_buf_pool *rx_data, *tx_data;
    net_pkt_get_info(&rx, &tx, &rx_data, &tx_data);
    unsigned int contexts = 0;
    for (unsigned int i = 0; i < 500; ++i) {
        contexts = 0;
        net_context_foreach(used, &contexts);
        if (contexts == 0 && k_mem_slab_num_free_get(rx) == CONFIG_NET_PKT_RX_COUNT &&
            k_mem_slab_num_free_get(tx) == CONFIG_NET_PKT_TX_COUNT &&
            net_buf_get_available(rx_data) == CONFIG_NET_BUF_RX_COUNT &&
            net_buf_get_available(tx_data) == CONFIG_NET_BUF_TX_COUNT)
            break;
        k_sleep(K_MSEC(10));
    }
    printk("STACK_RESOURCES contexts=%u rx_pkt=%u tx_pkt=%u rx_buf=%zu tx_buf=%zu\n", contexts,
           k_mem_slab_num_free_get(rx), k_mem_slab_num_free_get(tx), net_buf_get_available(rx_data),
           net_buf_get_available(tx_data));
    printk("STACK_SETTLE elapsed_ms=%lld\n", (long long)(k_uptime_get() - started));
    return contexts == 0 && k_mem_slab_num_free_get(rx) == CONFIG_NET_PKT_RX_COUNT &&
                   k_mem_slab_num_free_get(tx) == CONFIG_NET_PKT_TX_COUNT &&
                   net_buf_get_available(rx_data) == CONFIG_NET_BUF_RX_COUNT &&
                   net_buf_get_available(tx_data) == CONFIG_NET_BUF_TX_COUNT
               ? 0
               : -ETIMEDOUT;
}
ZTEST(networking, test_real_stack_failures_and_cleanup) {
    zassert_equal(networking_resources_idle(), 0);
    zassert_equal(networking_stack_link_loss(), 0);
    zassert_equal(networking_stack_scenarios(), 0);
    zassert_equal(networking_resources_idle(), 0);
}
ZTEST(networking, test_resource_exhaustion_and_return) {
    int descriptors[CONFIG_NET_MAX_CONTEXTS];
    zassert_equal(networking_resources_idle(), 0);
    for (unsigned int i = 0; i < ARRAY_SIZE(descriptors); ++i) {
        descriptors[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        zassert_true(descriptors[i] >= 0);
    }
    zassert_equal(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP), -1);
    zassert_equal(errno, ENOENT, "Pinned native context pool reports ENOENT on exhaustion");
    printk("STACK_EXHAUSTION held=%u errno=%d\n", CONFIG_NET_MAX_CONTEXTS, errno);
    for (unsigned int i = 0; i < ARRAY_SIZE(descriptors); ++i)
        zassert_equal(close(descriptors[i]), 0);
    zassert_equal(networking_resources_idle(), 0);
    int fresh = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    zassert_true(fresh >= 0);
    zassert_equal(close(fresh), 0);
    zassert_equal(networking_resources_idle(), 0);
}
ZTEST_SUITE(networking, NULL, NULL, NULL, NULL, NULL);
