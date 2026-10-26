#include "client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int monotonic_ns(int64_t *out)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return errno != 0 ? -errno : -EIO;
    if (now.tv_sec < 0 || now.tv_sec > (INT64_MAX - 999999999) / 1000000000)
        return -EOVERFLOW;
    *out = (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
    return 0;
}

static int wait_ready(int fd, const int64_t *deadline, short events)
{
    for (;;) {
        int64_t now;
        int result = monotonic_ns(&now);
        if (result != 0)
            return result;
        if (now >= *deadline)
            return -ETIMEDOUT;
        int64_t ms = (*deadline - now) / 1000000 + ((*deadline - now) % 1000000 != 0);
        struct pollfd item = {.fd = fd, .events = events};
        result = poll(&item, 1, ms > INT_MAX ? INT_MAX : (int)ms);
        if (result < 0 && (errno == EINTR || errno == EAGAIN))
            continue;
        if (result < 0)
            return -errno;
        if (result == 0)
            return -ETIMEDOUT;
        result = monotonic_ns(&now);
        if (result != 0)
            return result;
        if (now >= *deadline)
            return -ETIMEDOUT;
        if ((item.revents & POLLNVAL) != 0)
            return -EBADF;
        if ((item.revents & (events | POLLERR | POLLHUP)) != 0)
            return 0;
        return -EIO;
    }
}

static int transfer(int fd, uint8_t *buffer, size_t size, bool send_data, bool datagram,
                    int64_t deadline)
{
    size_t offset = 0;
    do {
        int result = wait_ready(fd, &deadline, send_data ? POLLOUT : POLLIN);
        if (result != 0)
            return result;
        ssize_t count = send_data ? send(fd, buffer + offset, size - offset, 0)
                                  : recv(fd, buffer + offset, size - offset, 0);
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
            continue;
        if (count < 0)
            return -errno;
        if (datagram)
            return count == 32 ? 0 : (send_data ? -EIO : -EMSGSIZE);
        if (count == 0)
            return send_data ? -EIO : -ECONNRESET;
        offset += (size_t)count;
    } while (offset < size);
    return 0;
}

int networking_exchange(const char *ipv4, uint16_t port, bool datagram, int timeout_ms)
{
    if (ipv4 == NULL || timeout_ms <= 0 || port == 0)
        return -EINVAL;
    struct sockaddr_in peer = {.sin_family = AF_INET, .sin_port = htons(port)};
    if (inet_pton(AF_INET, ipv4, &peer.sin_addr) != 1)
        return -EINVAL;
    int64_t now;
    int result = monotonic_ns(&now);
    if (result != 0)
        return result;
    int64_t duration = (int64_t)timeout_ms * 1000000;
    int64_t deadline = now > INT64_MAX - duration ? INT64_MAX : now + duration;
    int fd =
        socket(AF_INET, datagram ? SOCK_DGRAM : SOCK_STREAM, datagram ? IPPROTO_UDP : IPPROTO_TCP);
    if (fd < 0)
        return -errno;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        result = -errno;
        goto cleanup;
    }
    if (connect(fd, (const struct sockaddr *)&peer, sizeof(peer)) < 0) {
        if (errno != EINPROGRESS) {
            result = -errno;
            goto cleanup;
        }
        result = wait_ready(fd, &deadline, POLLOUT);
        if (result != 0)
            goto cleanup;
        int error = 0;
        socklen_t length = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0) {
            result = -errno;
            goto cleanup;
        }
        if (error != 0) {
            result = -error;
            goto cleanup;
        }
    }
    uint8_t request[32] = "crux-portable-networking-echo";
    uint8_t response[33];
    result = transfer(fd, request, sizeof(request), true, datagram, deadline);
    if (result == 0)
        result = transfer(fd, response, datagram ? sizeof(response) : sizeof(request), false,
                          datagram, deadline);
    if (result == 0 && memcmp(request, response, sizeof(request)) != 0)
        result = -EBADMSG;
cleanup:
    /* A close error does not prove the numeric descriptor is still owned. */
    if (close(fd) < 0) {
        int error = errno;
        fprintf(stderr, "NETWORK_CLOSE cleanup_uncertain=1 error=%d retry_descriptor=0\n", error);
        return -error;
    }
    return result;
}
