/* Test-owned server facilities, deliberately outside the client-only profile. */
#include "stack_scenario.h"
#include "client.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            printf("STACK_FAIL line=%d errno=%d\n", __LINE__, errno);                              \
            return -1;                                                                             \
        }                                                                                          \
    } while (0)
static struct {
    int listener, mode, error, native_error, poll_result;
    size_t received;
    bool datagram;
} peer;
/* Modes: echo, silence, close, oversized datagram, zero-length datagram. */
static int ready(int fd, short event) {
    struct pollfd item = {.fd = fd, .events = event};
    peer.poll_result = poll(&item, 1, 2000);
    if (peer.poll_result < 0)
        peer.native_error = errno;
    return peer.poll_result > 0;
}
static void serve(void *unused) {
    (void)unused;
    int fd = peer.listener;
    uint8_t bytes[64] = {0};
    struct sockaddr_in sender;
    socklen_t length = sizeof(sender);
    if (!ready(fd, POLLIN)) {
        peer.error = 1;
        return;
    }
    if (!peer.datagram) {
        fd = accept(fd, NULL, NULL);
        if (fd < 0) {
            peer.error = 2;
            peer.native_error = errno;
            return;
        }
    }
    networking_resources_active();
    size_t count = 0;
    while (count < 32) {
        if (!ready(fd, POLLIN)) {
            peer.error = 3;
            break;
        }
        ssize_t n = peer.datagram
                        ? recvfrom(fd, bytes, sizeof(bytes), 0, (struct sockaddr *)&sender, &length)
                        : recv(fd, bytes + count, 32 - count, 0);
        if (n <= 0) {
            peer.error = 4;
            break;
        }
        count += (size_t)n;
        peer.received += (size_t)n;
        if (peer.datagram)
            break;
    }
    if (peer.mode == 1) {
        (void)poll(NULL, 0, 400);
    } else if (peer.mode != 2 && peer.error == 0) {
        if (peer.datagram) {
            size_t size = peer.mode == 3 ? 64 : (peer.mode == 4 ? 0 : count);
            if (sendto(fd, bytes, size, 0, (struct sockaddr *)&sender, length) != (ssize_t)size)
                peer.error = 5;
        } else {
            /* Deliberate stream fragmentation exercises actual short reads. */
            for (size_t offset = 0; offset < count;) {
                size_t amount = count - offset > 3 ? 3 : count - offset;
                if (!ready(fd, POLLOUT)) {
                    peer.error = 6;
                    break;
                }
                ssize_t n = send(fd, bytes + offset, amount, 0);
                if (n <= 0) {
                    peer.error = 7;
                    break;
                }
                offset += (size_t)n;
            }
        }
    }
    if (!peer.datagram && close(fd) != 0)
        peer.error = 8;
}
static int scenario(bool udp, int mode, bool runtime) {
    peer.datagram = udp;
    peer.mode = mode;
    peer.error = 0;
    peer.native_error = 0;
    peer.received = 0;
    peer.listener = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    CHECK(peer.listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = 0};
    CHECK(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    CHECK(bind(peer.listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(peer.listener, (struct sockaddr *)&address, &size) == 0);
    if (!udp)
        CHECK(listen(peer.listener, 1) == 0);
    networking_peer_start(serve, NULL);
    if (mode == 5)
        networking_fixture_link(false);
    int result = runtime ? runtime_port_exchange("127.0.0.1", ntohs(address.sin_port))
                         : networking_exchange("127.0.0.1", ntohs(address.sin_port), udp, 250);
    CHECK(networking_peer_join() == 0);
    CHECK(close(peer.listener) == 0);
    CHECK(networking_resources_idle() == 0);
    if (mode == 5) {
        networking_fixture_link(true);
        printf("STACK_LINK_LOSS result=%d peer_error=%d native_error=%d received=%zu\n", result,
               peer.error, peer.native_error, peer.received);
        CHECK(peer.received == 0);
        CHECK((peer.error == 1 && peer.poll_result == 0) ||
              (peer.error == 2 &&
               (peer.native_error == ENETDOWN || peer.native_error == ENETUNREACH)));
        CHECK(result == -ENETDOWN || result == -ENETUNREACH || result == -ETIMEDOUT);
        return 0;
    }
    CHECK(peer.error == 0);
    int expected = mode == 1                  ? -ETIMEDOUT
                   : mode == 2                ? -ECONNRESET
                   : (mode == 3 || mode == 4) ? -EMSGSIZE
                                              : 0;
    printf("STACK_CASE udp=%d mode=%d runtime=%d result=%d expected=%d\n", udp, mode, runtime,
           result, expected);
    CHECK(result == expected);
    return 0;
}
int networking_stack_link_loss(void) {
    CHECK(scenario(false, 5, false) == 0);
    CHECK(scenario(false, 0, false) == 0);
    CHECK(scenario(true, 0, false) == 0);
    return 0;
}
static int zero_datagram(void) {
    int server = socket(AF_INET, SOCK_DGRAM, 0);
    int client = socket(AF_INET, SOCK_DGRAM, 0);
    CHECK(server >= 0 && client >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET};
    CHECK(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    CHECK(bind(server, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(server, (struct sockaddr *)&address, &size) == 0);
    CHECK(connect(client, (struct sockaddr *)&address, size) == 0);
    CHECK(send(client, "", 0, 0) == 0);
    CHECK(ready(server, POLLIN));
    char byte;
    CHECK(recvfrom(server, &byte, 1, 0, (struct sockaddr *)&address, &size) == 0);
    CHECK(sendto(server, "", 0, 0, (struct sockaddr *)&address, size) == 0);
    CHECK(ready(client, POLLIN));
    CHECK(recv(client, &byte, 1, 0) == 0);
    CHECK(close(client) == 0 && close(server) == 0);
    puts("STACK_ZERO_DATAGRAM pass=1");
    return 0;
}
static int send_after_peer_close(void) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(listener >= 0);
    struct sockaddr_in address = {.sin_family = AF_INET};
    CHECK(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    CHECK(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address);
    CHECK(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    CHECK(listen(listener, 1) == 0);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(client >= 0);
    CHECK(fcntl(client, F_SETFL, O_NONBLOCK) == 0);
    int result = connect(client, (struct sockaddr *)&address, size);
    CHECK(result == 0 || errno == EINPROGRESS);
    CHECK(ready(listener, POLLIN));
    int accepted = accept(listener, NULL, NULL);
    CHECK(accepted >= 0);
    CHECK(close(accepted) == 0);
    CHECK(ready(client, POLLIN));
    char byte;
    CHECK(recv(client, &byte, 1, 0) == 0);
    bool failed = false;
    for (unsigned int attempt = 0; attempt < 20; ++attempt) {
        ssize_t n = send(client, "x", 1, 0);
        if (n < 0 && (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN)) {
            printf("STACK_POST_CLOSE errno=%d attempts=%u\n", errno, attempt + 1);
            failed = true;
            break;
        }
        (void)poll(NULL, 0, 10);
    }
    CHECK(close(client) == 0);
    CHECK(close(listener) == 0);
    CHECK(failed);
    return 0;
}
int networking_stack_scenarios(void) {
    for (int repeat = 0; repeat < 3; ++repeat) {
        CHECK(scenario(false, 0, false) == 0);
        CHECK(scenario(true, 0, false) == 0);
        CHECK(scenario(false, 1, false) == 0);
        CHECK(scenario(true, 1, false) == 0);
        CHECK(scenario(false, 2, false) == 0);
        CHECK(scenario(true, 3, false) == 0);
        CHECK(scenario(true, 4, false) == 0);
        CHECK(scenario(false, 0, true) == 0);
        /* Fresh connection after all error cases, not automatic socket recovery. */
        CHECK(scenario(false, 0, false) == 0);
        CHECK(scenario(true, 0, false) == 0);
    }
    CHECK(zero_datagram() == 0);
    CHECK(send_after_peer_close() == 0);
    /* Reserve a TCP port, then close it to obtain a known absent listener. */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in absent = {.sin_family = AF_INET};
    CHECK(inet_pton(AF_INET, "127.0.0.1", &absent.sin_addr) == 1);
    CHECK(bind(fd, (struct sockaddr *)&absent, sizeof(absent)) == 0);
    socklen_t size = sizeof(absent);
    CHECK(getsockname(fd, (struct sockaddr *)&absent, &size) == 0);
    CHECK(close(fd) == 0);
    CHECK(networking_exchange("127.0.0.1", ntohs(absent.sin_port), false, 250) == -ECONNREFUSED);
    return 0;
}
