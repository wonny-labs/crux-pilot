/*
 * The server's inactivity timer closes a client with zsock_shutdown(), and the native socket
 * offload at v4.4.2 has no shutdown operation, so on native_sim the call fails and the timer does
 * nothing: an idle keep-alive connection keeps its client slot for ever. Issue #220 measured that
 * on this image without this file: three idle connections, and a fourth client's handshake timed
 * out after 30 s.
 *
 * This gives the timer the effect it has on Zephyr's own TCP stack, as the shim of
 * tests/native/web_http/src/socket_shim.c does for #184's and #185's images, with only its shutdown
 * part: no probe, and no send is changed. Over TLS the server's descriptor is a TLS socket that
 * holds the offloaded one, and the TLS layer calls shutdown again for it, which lands here. Test
 * image only, because a product does not use the offload.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/fdtable.h>

bool net_socket_is_tls(void *obj);
int crux_e2e_host_shutdown(int host_fd, int how);
int __real_z_impl_zsock_shutdown(int sock, int how);
int __wrap_z_impl_zsock_shutdown(int sock, int how);

/* The start of the driver's private struct nsos_socket at v4.4.2, up to the host descriptor, as in
 * socket_shim.c. A wrong layout makes the host call fail, and the slot case then fails. */
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

int __wrap_z_impl_zsock_shutdown(int sock, int how)
{
    const struct nsos_socket_start *socket = zvfs_get_fd_obj(sock, NULL, EBADF);

    if (socket == NULL) {
        return -1;
    }
    if (net_socket_is_tls((void *)socket)) {
        return __real_z_impl_zsock_shutdown(sock, how);
    }
    return crux_e2e_host_shutdown(socket->poll_mid.fd, how);
}
