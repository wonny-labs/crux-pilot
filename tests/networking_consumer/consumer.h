#ifndef NETWORKING_CONSUMER_H
#define NETWORKING_CONSUMER_H
#include <crux/netif.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
static int run_consumer(void)
{
    crux_netif_status_t status;
    if (crux_netif_get_status(&status) != CRUX_OK)
        return 1;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return 2;
    int flags = fcntl(fd, F_GETFL, 0);
    int result = flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0;
    struct pollfd item = {fd, POLLOUT, 0};
    if (poll(&item, 1, 0) < 0)
        result = 1;
    if (close(fd) != 0)
        result = 1;
    return result;
}
#endif
