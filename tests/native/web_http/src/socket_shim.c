/* Two things this test image does at the socket call that the server makes.
 *
 * The server's inactivity timer closes a client with zsock_shutdown(), and the native socket
 * offload at v4.4.2 has no shutdown operation, so there the call fails and the timer does nothing.
 * This gives the timer the effect it has on Zephyr's own TCP stack, so that the measurements mean
 * something.
 *
 * The offload's send waits out a timeout and then sends anyway, so the server rarely sees -EAGAIN,
 * which it swallows and which leaves a client held. A probe route can make one chosen send return
 * it, and can shrink a socket's send buffer, as on a small target.
 *
 * Over TLS the server's descriptor is a TLS socket, which holds the offloaded one, and the TLS
 * layer calls these again for it. A shutdown goes on to that socket, a send counts and fails only
 * at the outermost call, which is the server's, and a send buffer applies to the offloaded socket.
 *
 * It is here and not in the adapter because a product does not use the offload.
 */
#include "web_probe.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/fdtable.h>

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
bool net_socket_is_tls(void *obj);
#else
#define net_socket_is_tls(obj) false
#endif

int web_host_shutdown(int host_fd, int how);
int web_host_set_send_buffer(int host_fd, int size);
int __real_z_impl_zsock_shutdown(int sock, int how);
int __wrap_z_impl_zsock_shutdown(int sock, int how);
ssize_t __real_z_impl_zsock_sendto(int sock, const void *buf, size_t len, int flags,
                                   const struct net_sockaddr *dest_addr, net_socklen_t addrlen);
ssize_t __wrap_z_impl_zsock_sendto(int sock, const void *buf, size_t len, int flags,
                                   const struct net_sockaddr *dest_addr, net_socklen_t addrlen);

/* The start of the driver's private struct nsos_socket at v4.4.2, up to the host descriptor. A
 * wrong layout makes the host call fail, and the driver's inactivity cases then fail. */
struct nsos_socket_start {
    int fd;
    k_timeout_t recv_timeout;
    k_timeout_t send_timeout;
    struct {
        int fd;
        short events;
        short revents;
        void (*cb)(void *pollfd_mid);
    } poll_mid;
};

static bool fail_armed;
static unsigned fail_skip;
static int shrink_size;
static unsigned eagain_count;
static unsigned send_depth;
static unsigned outer_sends;
static unsigned inner_sends;
static unsigned real_eagain;

int __wrap_z_impl_zsock_shutdown(int sock, int how)
{
    const struct nsos_socket_start *socket = zvfs_get_fd_obj(sock, NULL, EBADF);

    if (socket == NULL) {
        return -1;
    }
    if (net_socket_is_tls((void *)socket)) {
        return __real_z_impl_zsock_shutdown(sock, how);
    }
    return web_host_shutdown(socket->poll_mid.fd, how);
}

ssize_t __wrap_z_impl_zsock_sendto(int sock, const void *buf, size_t len, int flags,
                                   const struct net_sockaddr *dest_addr, net_socklen_t addrlen)
{
    const struct nsos_socket_start *socket = zvfs_get_fd_obj(sock, NULL, EBADF);
    ssize_t sent;

    if (shrink_size != 0 && socket != NULL && !net_socket_is_tls((void *)socket)) {
        (void)web_host_set_send_buffer(socket->poll_mid.fd, shrink_size);
        shrink_size = 0;
    }
    if (send_depth == 0u) {
        ++outer_sends;
        if (fail_armed) {
            if (fail_skip == 0u) {
                fail_armed = false;
                ++eagain_count;
                errno = EAGAIN;
                return -1;
            }
            --fail_skip;
        }
    } else {
        ++inner_sends;
    }
    ++send_depth;
    sent = __real_z_impl_zsock_sendto(sock, buf, len, flags, dest_addr, addrlen);
    --send_depth;
    if (send_depth == 0u && sent < 0 && errno == EAGAIN) {
        ++real_eagain;
    }
    return sent;
}

void web_probe_fail_send_after(unsigned skip)
{
    fail_skip = skip;
    fail_armed = true;
}

void web_probe_set_next_send_buffer(int size)
{
    shrink_size = size;
}

unsigned web_probe_eagain_count(void)
{
    return eagain_count;
}

unsigned web_probe_outer_sends(void)
{
    return outer_sends;
}

unsigned web_probe_inner_sends(void)
{
    return inner_sends;
}

unsigned web_probe_real_eagain(void)
{
    return real_eagain;
}
