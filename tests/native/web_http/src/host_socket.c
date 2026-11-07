/* Runs on the host side of native_sim. See socket_shim.c. */
#include <sys/socket.h>

int web_host_shutdown(int host_fd, int how);
int web_host_set_send_buffer(int host_fd, int size);

int web_host_shutdown(int host_fd, int how)
{
    return shutdown(host_fd, how);
}

int web_host_set_send_buffer(int host_fd, int size)
{
    return setsockopt(host_fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
}
