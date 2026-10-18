#include <zephyr/ztest.h>

/* Real native sockets and production exchange; no syscall or clock mocks. */
#include "../../../samples/controller/zephyr/network_client.c"

static struct k_thread tcp_thread;
static struct k_thread udp_thread;
K_THREAD_STACK_DEFINE(tcp_stack, 4096);
K_THREAD_STACK_DEFINE(udp_stack, 4096);
K_SEM_DEFINE(peer_ready, 0, 2);
static int peer_error;

static void echo_peer(void *kind, void *unused1, void *unused2) {
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);
    unsigned int protocol = (uintptr_t)kind;
    int listener = zsock_socket(AF_INET, protocol == 1 ? SOCK_STREAM : SOCK_DGRAM,
                                protocol == 1 ? IPPROTO_TCP : IPPROTO_UDP);
    struct sockaddr_in address = {.sin_family = AF_INET,
                                  .sin_port = htons(protocol == 1 ? 42420 : 42421)};
    net_addr_pton(AF_INET, "192.168.77.1", &address.sin_addr);
    if (listener < 0 || zsock_bind(listener, (struct sockaddr *)&address, sizeof(address)) < 0 ||
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
        if (zsock_poll(&poller, 1, 5000) <= 0) {
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
                result = stream_transfer(client, frame, sizeof(frame), true, deadline);
            }
            printk("STACK_PEER protocol=1 sequence=%u result=%d\n", i + 1, result);
            zsock_close(client);
        } else {
            struct sockaddr_in sender;
            socklen_t size = sizeof(sender);
            ssize_t length = zsock_recvfrom(listener, frame, sizeof(frame), 0,
                                            (struct sockaddr *)&sender, &size);
            if (length == sizeof(frame)) {
                zsock_sendto(listener, frame, sizeof(frame), 0, (struct sockaddr *)&sender, size);
            }
            printk("STACK_PEER protocol=2 sequence=%u length=%zd\n", i + 1, length);
        }
    }
    zsock_close(listener);
}

ZTEST(network_stack, test_native_round_and_cleanup) {
    struct net_if *loopback = net_if_get_default();
    struct net_in_addr address;
    zassert_equal(net_addr_pton(AF_INET, "192.168.77.1", &address), 0);
    zassert_not_null(net_if_ipv4_addr_add(loopback, &address, NET_ADDR_MANUAL, 0));
    zassert_equal(controller_network_init(), 0);
    k_thread_create(&tcp_thread, tcp_stack, K_THREAD_STACK_SIZEOF(tcp_stack), echo_peer,
                    (void *)(uintptr_t)1, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_create(&udp_thread, udp_stack, K_THREAD_STACK_SIZEOF(udp_stack), echo_peer,
                    (void *)(uintptr_t)2, NULL, NULL, 5, 0, K_NO_WAIT);
    zassert_equal(k_sem_take(&peer_ready, K_SECONDS(2)), 0);
    zassert_equal(k_sem_take(&peer_ready, K_SECONDS(2)), 0);
    zassert_equal(peer_error, 0);
    controller_network_report();
    int result = controller_network_round(1);
    printk("STACK_ROUND result=%d\n", result);
    zassert_equal(k_thread_join(&tcp_thread, K_SECONDS(8)), 0);
    zassert_equal(k_thread_join(&udp_thread, K_SECONDS(8)), 0);
    for (unsigned int i = 0; i <= 12; ++i) {
        controller_network_report();
        k_sleep(K_MSEC(500));
    }
    zassert_equal(owned_sockets, 0);
    zassert_equal(result, 0, "Native round failed; preserve resource observations");
}

ZTEST_SUITE(network_stack, NULL, NULL, NULL, NULL, NULL);
