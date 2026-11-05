/* The server's inactivity timer closes a client with zsock_shutdown(), and the native socket
 * offload at v4.4.2 has no shutdown operation, so there the call fails and the timer does nothing.
 * This test image gives the timer the effect it has on Zephyr's own TCP stack, so that the
 * measurements mean something. It is here and not in the adapter because a product does not use the
 * offload. */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/fdtable.h>

int web_host_shutdown(int host_fd, int how);
int __wrap_z_impl_zsock_shutdown(int sock, int how);

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

int __wrap_z_impl_zsock_shutdown(int sock, int how)
{
    const struct nsos_socket_start *socket = zvfs_get_fd_obj(sock, NULL, EBADF);

    if (socket == NULL) {
        return -1;
    }
    return web_host_shutdown(socket->poll_mid.fd, how);
}
