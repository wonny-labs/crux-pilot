/* Runs on the host side of native_sim. See offload_shutdown.c. */
#include <sys/socket.h>

int crux_e2e_host_shutdown(int host_fd, int how);

int crux_e2e_host_shutdown(int host_fd, int how)
{
    return shutdown(host_fd, how);
}
