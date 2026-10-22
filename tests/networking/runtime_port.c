/* Independently written runtime-shaped consumer: no sample/private/native API. */
#include "runtime_port.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int wait_until(int fd, short events, const struct timespec *start) {
    for (;;) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return -1;
        long long elapsed =
            (long long)(now.tv_sec - start->tv_sec) * 1000000000 + now.tv_nsec - start->tv_nsec;
        if (elapsed >= 200000000) {
            errno = ETIMEDOUT;
            return -1;
        }
        struct pollfd item = {.fd = fd, .events = events};
        int result = poll(&item, 1, (int)((200000000 - elapsed + 999999) / 1000000));
        if (result < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (result <= 0) {
            if (result == 0)
                errno = ETIMEDOUT;
            return -1;
        }
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
            return -1;
        elapsed =
            (long long)(now.tv_sec - start->tv_sec) * 1000000000 + now.tv_nsec - start->tv_nsec;
        if (elapsed >= 200000000) {
            errno = ETIMEDOUT;
            return -1;
        }
        if (item.revents & POLLNVAL) {
            errno = EBADF;
            return -1;
        }
        if (item.revents & (events | POLLERR | POLLHUP))
            return 0;
        errno = EIO;
        return -1;
    }
}
int runtime_port_exchange(const char *address, unsigned short port) {
    struct sockaddr_in endpoint = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (inet_pton(AF_INET, address, &endpoint.sin_addr) != 1)
        return -EINVAL;
    struct timespec start;
    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
        return -errno;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0)
        return -errno;
    int result = -1;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0)
        goto done;
    if (connect(fd, (const struct sockaddr *)&endpoint, sizeof(endpoint)) != 0) {
        if (errno != EINPROGRESS || wait_until(fd, POLLOUT, &start) != 0)
            goto done;
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) != 0)
            goto done;
        if (error != 0) {
            errno = error;
            goto done;
        }
    }
    unsigned char payload[32] = "independent-runtime-port-fixture";
    unsigned char reply[32];
    for (int direction = 0; direction < 2; ++direction) {
        size_t offset = 0;
        while (offset < sizeof(payload)) {
            if (wait_until(fd, direction == 0 ? POLLOUT : POLLIN, &start) != 0)
                goto done;
            ssize_t n = direction == 0 ? send(fd, payload + offset, sizeof(payload) - offset, 0)
                                       : recv(fd, reply + offset, sizeof(reply) - offset, 0);
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            if (n <= 0) {
                if (n == 0)
                    errno = ECONNRESET;
                goto done;
            }
            offset += (size_t)n;
        }
    }
    if (memcmp(payload, reply, sizeof(payload)) != 0) {
        errno = EBADMSG;
        goto done;
    }
    result = 0;
done:
    if (result != 0)
        result = -errno;
    if (close(fd) != 0)
        return -errno;
    return result;
}
