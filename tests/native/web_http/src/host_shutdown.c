/* Runs on the host side of native_sim. See shutdown_shim.c. */
#include <sys/socket.h>

int web_host_shutdown(int host_fd, int how);

int web_host_shutdown(int host_fd, int how)
{
    return shutdown(host_fd, how);
}
