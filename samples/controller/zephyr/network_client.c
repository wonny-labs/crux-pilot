#include "network_client.h"

#include <zephyr/kernel.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

static struct net_if *interface;
static unsigned int owned_sockets;
static bool address_ready;
static bool service_ready;

static int wait_socket(int fd, short events, int64_t deadline) {
    for (;;) {
        int64_t remaining = deadline - k_uptime_get();
        if (remaining <= 0) {
            return -ETIMEDOUT;
        }
        struct zsock_pollfd item = {.fd = fd, .events = events};
        int result = zsock_poll(&item, 1, (int)remaining);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return -errno;
        }
        if (result == 0) {
            return -ETIMEDOUT;
        }
        /* Read EOF/error through recv or SO_ERROR, rather than treating HUP as success. */
        if ((item.revents & (events | ZSOCK_POLLERR | ZSOCK_POLLHUP)) != 0) {
            return 0;
        }
        return -EIO;
    }
}

static int stream_transfer(int fd, uint8_t *data, size_t length, bool transmit, int64_t deadline) {
    size_t offset = 0;
    while (offset < length) {
        int result = wait_socket(fd, transmit ? ZSOCK_POLLOUT : ZSOCK_POLLIN, deadline);
        if (result < 0) {
            return result;
        }
        ssize_t count = transmit ? zsock_send(fd, data + offset, length - offset, 0)
                                 : zsock_recv(fd, data + offset, length - offset, 0);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (count < 0) {
            return -errno;
        }
        if (count == 0) {
            return -ECONNRESET;
        }
        offset += (size_t)count;
    }
    return 0;
}

static int exchange(unsigned int protocol, unsigned int trial, unsigned int sequence) {
    uint8_t request[32] = {'C', 'M', '3', 'N'};
    uint8_t response[33];
    sys_put_be32(protocol, request + 4);
    sys_put_be32(trial, request + 8);
    sys_put_be32(sequence, request + 12);
    memcpy(request + 16, "crux-m3-payload!", 16);
    struct sockaddr_in peer = {.sin_family = AF_INET,
                               .sin_port = htons(protocol == 1 ? 42420 : 42421)};
    if (zsock_inet_pton(AF_INET, "192.168.77.1", &peer.sin_addr) != 1) {
        return -EINVAL;
    }
    int64_t deadline = k_uptime_get() + 2000;
    int fd = zsock_socket(AF_INET, protocol == 1 ? SOCK_STREAM : SOCK_DGRAM,
                          protocol == 1 ? IPPROTO_TCP : IPPROTO_UDP);
    if (fd < 0) {
        return -errno;
    }
    owned_sockets++;
    int result = 0;
    if (zsock_fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        result = -errno;
        goto close;
    }
    if (zsock_connect(fd, (struct sockaddr *)&peer, sizeof(peer)) < 0) {
        if (errno != EINPROGRESS) {
            result = -errno;
            goto close;
        }
        result = wait_socket(fd, ZSOCK_POLLOUT, deadline);
        if (result < 0) {
            goto close;
        }
        int error = 0;
        socklen_t size = sizeof(error);
        if (zsock_getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) {
            result = -errno;
            goto close;
        }
        if (error != 0) {
            result = -error;
            goto close;
        }
    }
    if (protocol == 1) {
        result = stream_transfer(fd, request, sizeof(request), true, deadline);
        if (result == 0) {
            result = stream_transfer(fd, response, sizeof(request), false, deadline);
        }
    } else {
        result = wait_socket(fd, ZSOCK_POLLOUT, deadline);
        if (result == 0) {
            ssize_t sent = zsock_send(fd, request, sizeof(request), 0);
            result = sent == sizeof(request) ? 0 : (sent < 0 ? -errno : -EIO);
        }
        if (result == 0) {
            result = wait_socket(fd, ZSOCK_POLLIN, deadline);
        }
        if (result == 0) {
            /* Connected UDP restricts the source;33 bytes detects oversized datagrams. */
            ssize_t received = zsock_recv(fd, response, sizeof(response), 0);
            result = received == sizeof(request) ? 0 : (received < 0 ? -errno : -EMSGSIZE);
        }
    }
    if (result == 0 && memcmp(request, response, sizeof(request)) != 0) {
        result = -EBADMSG;
    }
close:
    if (zsock_close(fd) < 0) {
        /* Preserve uncertain ownership; do not silently accept the idle baseline. */
        return -errno;
    }
    owned_sockets--;
    return result;
}

static void count_context(struct net_context *context, void *value) {
    if (net_context_is_used(context)) {
        (*(unsigned int *)value)++;
    }
}

void controller_network_report(void) {
    struct k_mem_slab *rx;
    struct k_mem_slab *tx;
    struct net_buf_pool *rx_data;
    struct net_buf_pool *tx_data;
    unsigned int contexts = 0;
    net_context_foreach(count_context, &contexts);
    net_pkt_get_info(&rx, &tx, &rx_data, &tx_data);
    printk("M3_NET_RESOURCE monotonic_ms=%lld owned=%u contexts=%u rx_pkt_free=%u "
           "tx_pkt_free=%u rx_buf_free=%zu tx_buf_free=%zu\n",
           (long long)k_uptime_get(), owned_sockets, contexts, k_mem_slab_num_free_get(rx),
           k_mem_slab_num_free_get(tx), net_buf_get_available(rx_data),
           net_buf_get_available(tx_data));
    bool link = interface != NULL && net_if_is_carrier_ok(interface);
    service_ready = service_ready && link;
    printk("M3_NET_STATE link=%d address=%d service_ready=%d\n", link, address_ready,
           service_ready);
}

int controller_network_init(void) {
    address_ready = false;
    service_ready = false;
    if (owned_sockets != 0) {
        return -EBUSY;
    }
    interface = net_if_get_default();
    if (interface == NULL) {
        return -ENODEV;
    }
    struct net_in_addr address;
    struct net_in_addr mask;
    if (net_addr_pton(AF_INET, "192.168.77.2", &address) != 0 ||
        net_addr_pton(AF_INET, "255.255.255.0", &mask) != 0 ||
        net_if_ipv4_addr_add(interface, &address, NET_ADDR_MANUAL, 0) == NULL ||
        !net_if_ipv4_set_netmask_by_addr(interface, &address, &mask)) {
        interface = NULL;
        return -EADDRNOTAVAIL;
    }
    int result = net_if_up(interface);
    address_ready = result == 0 || result == -EALREADY;
    return address_ready ? 0 : result;
}

int controller_network_round(unsigned int trial) {
    if (interface == NULL || !address_ready || trial == 0) {
        return -EINVAL;
    }
    if (owned_sockets != 0) {
        return -EBUSY;
    }
    service_ready = false;
    controller_network_report();
    int failures = 0;
    for (unsigned int protocol = 1; protocol <= 2; ++protocol) {
        for (unsigned int sequence = 1; sequence <= 10; ++sequence) {
            int64_t start = k_uptime_get();
            int result = exchange(protocol, trial, sequence);
            printk("M3_NET_EXCHANGE trial=%u protocol=%u sequence=%u result=%d "
                   "monotonic_ms=%lld elapsed_ms=%lld\n",
                   trial, protocol, sequence, result, (long long)k_uptime_get(),
                   (long long)(k_uptime_get() - start));
            failures += result != 0;
            controller_network_report();
            if (owned_sockets != 0) {
                return -EBUSY;
            }
        }
    }
    service_ready = failures == 0;
    controller_network_report();
    return failures == 0 ? 0 : -EIO;
}
