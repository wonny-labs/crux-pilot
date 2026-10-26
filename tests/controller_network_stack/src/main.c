#include <zephyr/ztest.h>

/* Real native sockets and production exchange; no syscall or clock mocks. */
#include "../../../samples/controller/zephyr/network_client.c"

static struct k_thread tcp_thread;
static struct k_thread udp_thread;
K_THREAD_STACK_DEFINE(tcp_stack, 4096);
K_THREAD_STACK_DEFINE(udp_stack, 4096);
K_SEM_DEFINE(peer_ready, 0, 2);
K_SEM_DEFINE(round_done, 0, 2);
static int peer_error;
static enum { PEER_ECHO, PEER_CLOSE, PEER_SILENCE } peer_mode;
static unsigned int peer_frames[2];
/* Upstream loopback test control: both endpoints are real local addresses. */
extern void loopback_enable_address_swap(bool swap_addresses);

static void assert_idle_pools(void)
{
    struct k_mem_slab *rx;
    struct k_mem_slab *tx;
    struct net_buf_pool *rx_data;
    struct net_buf_pool *tx_data;
    net_pkt_get_info(&rx, &tx, &rx_data, &tx_data);
    zassert_equal(k_mem_slab_num_free_get(rx), CONFIG_NET_PKT_RX_COUNT);
    zassert_equal(k_mem_slab_num_free_get(tx), CONFIG_NET_PKT_TX_COUNT);
    zassert_equal(net_buf_get_available(rx_data), CONFIG_NET_BUF_RX_COUNT);
    zassert_equal(net_buf_get_available(tx_data), CONFIG_NET_BUF_TX_COUNT);
}

static bool valid_frame(const uint8_t *frame, unsigned int protocol, unsigned int sequence)
{
    return memcmp(frame, "CM3N", 4) == 0 && sys_get_be32(frame + 4) == protocol &&
           sys_get_be32(frame + 8) == 1 && sys_get_be32(frame + 12) == sequence &&
           memcmp(frame + 16, "crux-m3-payload!", 16) == 0;
}

static void echo_peer(void *kind, void *unused1, void *unused2)
{
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);
    unsigned int protocol = (uintptr_t)kind;
    int listener = zsock_socket(NET_AF_INET, protocol == 1 ? NET_SOCK_STREAM : NET_SOCK_DGRAM,
                                protocol == 1 ? NET_IPPROTO_TCP : NET_IPPROTO_UDP);
    struct net_sockaddr_in address = {.sin_family = NET_AF_INET,
                                      .sin_port = net_htons(protocol == 1 ? 42420 : 42421)};
    net_addr_pton(NET_AF_INET, "192.168.77.1", &address.sin_addr);
    if (listener < 0 ||
        zsock_bind(listener, (struct net_sockaddr *)&address, sizeof(address)) < 0 ||
        (protocol == 1 && zsock_listen(listener, 1) < 0)) {
        peer_error = errno;
        k_sem_give(&peer_ready);
        if (listener >= 0) {
            zsock_close(listener);
        }
        return;
    }
    k_sem_give(&peer_ready);
    for (unsigned int i = 0; i < 10; ++i) {
        struct zsock_pollfd poller = {.fd = listener, .events = ZSOCK_POLLIN};
        if (zsock_poll(&poller, 1, 150000) <= 0) {
            break;
        }
        uint8_t frame[32];
        if (protocol == 1) {
            int client = zsock_accept(listener, NULL, NULL);
            if (client < 0) {
                peer_error = errno;
                break;
            }
            int64_t deadline = k_uptime_get() + 2000;
            int result = stream_transfer(client, frame, sizeof(frame), false, deadline);
            if (result == 0) {
                if (!valid_frame(frame, protocol, i + 1)) {
                    peer_error = EBADMSG;
                    result = -EBADMSG;
                } else {
                    peer_frames[0]++;
                    if (peer_mode == PEER_ECHO) {
                        result = stream_transfer(client, frame, sizeof(frame), true, deadline);
                    } else if (peer_mode == PEER_SILENCE) {
                        k_sleep(K_MSEC(2250));
                    }
                }
            }
            printk("STACK_PEER protocol=1 sequence=%u result=%d\n", i + 1, result);
            zsock_close(client);
        } else {
            struct net_sockaddr_in sender;
            net_socklen_t size = sizeof(sender);
            ssize_t length = zsock_recvfrom(listener, frame, sizeof(frame), 0,
                                            (struct net_sockaddr *)&sender, &size);
            if (length == sizeof(frame) && valid_frame(frame, protocol, i + 1)) {
                peer_frames[1]++;
                if (peer_mode != PEER_SILENCE) {
                    zsock_sendto(listener, frame, sizeof(frame), 0, (struct net_sockaddr *)&sender,
                                 size);
                }
            } else {
                peer_error = EBADMSG;
            }
            printk("STACK_PEER protocol=2 sequence=%u length=%zd\n", i + 1, length);
        }
    }
    k_sem_take(&round_done, K_SECONDS(150));
    zsock_close(listener);
}

static void run_fixture(int mode)
{
    zassert_equal(native_contexts(), 0, "No old context may be hidden by a new baseline");
    assert_idle_pools();
    zassert_equal(k_sem_count_get(&peer_ready), 0);
    zassert_equal(k_sem_count_get(&round_done), 0);
    peer_mode = mode;
    peer_error = 0;
    peer_frames[0] = peer_frames[1] = 0;
    loopback_enable_address_swap(false);
    struct net_if *loopback = net_if_get_default();
    struct net_in_addr address;
    zassert_equal(net_addr_pton(NET_AF_INET, "192.168.77.1", &address), 0);
    zassert_not_null(net_if_ipv4_addr_add(loopback, &address, NET_ADDR_MANUAL, 0));
    k_thread_create(&tcp_thread, tcp_stack, K_THREAD_STACK_SIZEOF(tcp_stack), echo_peer,
                    (void *)(uintptr_t)1, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_create(&udp_thread, udp_stack, K_THREAD_STACK_SIZEOF(udp_stack), echo_peer,
                    (void *)(uintptr_t)2, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&peer_ready, K_SECONDS(2)), 0);
    zassert_equal(k_sem_take(&peer_ready, K_SECONDS(2)), 0);
    zassert_equal(peer_error, 0);
    zassert_equal(controller_network_init(), 0);
    zassert_equal(idle_contexts, 2, "Only the two fixture listeners belong in the baseline");
    controller_network_report();
    int result = controller_network_round(1);
    printk("STACK_ROUND result=%d\n", result);
    k_sem_give(&round_done);
    k_sem_give(&round_done);
    zassert_equal(k_thread_join(&tcp_thread, K_SECONDS(8)), 0);
    zassert_equal(k_thread_join(&udp_thread, K_SECONDS(8)), 0);
    for (unsigned int i = 0; i <= 12; ++i) {
        controller_network_report();
        k_sleep(K_MSEC(500));
    }
    zassert_equal(owned_sockets, 0);
    zassert_equal(native_contexts(), 0);
    assert_idle_pools();
    zassert_equal(peer_error, 0);
    zassert_equal(peer_frames[0], 10);
    zassert_equal(peer_frames[1], 10);
    zassert_equal(result, mode == PEER_ECHO ? 0 : -EIO,
                  "Unexpected native round outcome; preserve resource observations");
}

ZTEST(network_stack, test_native_round_and_cleanup)
{
    run_fixture(PEER_ECHO);
}
ZTEST(network_stack, test_native_peer_closure_and_cleanup)
{
    run_fixture(PEER_CLOSE);
}
ZTEST(network_stack, test_native_no_response_and_cleanup)
{
    run_fixture(PEER_SILENCE);
}

ZTEST_SUITE(network_stack, NULL, NULL, NULL, NULL, NULL);
