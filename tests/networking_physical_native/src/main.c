#include "fixture.h"
#include "frame.h"
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zephyr/net/net_if.h>
#include <zephyr/ztest.h>
static struct k_thread peer;
K_THREAD_STACK_DEFINE(peer_stack, 2048);
K_SEM_DEFINE(ready, 0, 1);
static int kind, behavior;
static void serve(void *a, void *b, void *c) {
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    int fd = socket(AF_INET, kind, 0);
    zassert_true(fd >= 0);
    struct sockaddr_in local = {
        .sin_family = AF_INET, .sin_port = htons(42422), .sin_addr.s_addr = htonl(0x7f000001U)};
    zassert_equal(bind(fd, (struct sockaddr *)&local, sizeof(local)), 0);
    if (kind == SOCK_STREAM)
        zassert_equal(listen(fd, 1), 0);
    k_sem_give(&ready);
    int stream = kind == SOCK_STREAM ? accept(fd, NULL, NULL) : fd;
    zassert_true(stream >= 0);
    uint8_t frame[32];
    struct sockaddr_in source;
    socklen_t length = sizeof(source);
    if (kind == SOCK_STREAM) {
        size_t used = 0;
        while (used < sizeof(frame)) {
            ssize_t got = recv(stream, frame + used, sizeof(frame) - used, 0);
            zassert_true(got > 0);
            used += (size_t)got;
        }
    } else {
        zassert_equal(recvfrom(fd, frame, sizeof(frame), 0, (struct sockaddr *)&source, &length),
                      32);
    }
    if (behavior == 0) {
        if (kind == SOCK_STREAM) {
            for (size_t i = 0; i < sizeof(frame); ++i)
                zassert_equal(send(stream, frame + i, 1, 0), 1);
        } else {
            zassert_equal(sendto(fd, frame, sizeof(frame), 0, (struct sockaddr *)&source, length),
                          32);
        }
    } else if (behavior == 1) {
        k_sleep(K_MSEC(400));
    }
    if (stream != fd)
        zassert_equal(close(stream), 0);
    zassert_equal(close(fd), 0);
}
ZTEST(m5_fixture, test_actual_native_consumer_and_accounting) {
    zassert_equal(m5_fixture_init(), 0);
    struct fixture_session session = {.token = 7};
    struct fixture_command idle = {.action = 'I', .sequence = 1};
    zassert_equal(m5_fixture_execute(&session, &idle), 0);
    const int modes[][3] = {{SOCK_STREAM, 0, 0},
                            {SOCK_DGRAM, 0, 0},
                            {SOCK_STREAM, 1, -ETIMEDOUT},
                            {SOCK_STREAM, 2, -ECONNRESET}};
    for (size_t i = 0; i < ARRAY_SIZE(modes); ++i) {
        kind = modes[i][0];
        behavior = modes[i][1];
        k_thread_create(&peer, peer_stack, K_THREAD_STACK_SIZEOF(peer_stack), serve, NULL, NULL,
                        NULL, 5, 0, K_NO_WAIT);
        zassert_equal(k_sem_take(&ready, K_SECONDS(2)), 0);
        uint8_t frame[32];
        struct m5_metrics metrics;
        m5_frame(frame, 7, 4, (uint32_t)i + 1, kind == SOCK_DGRAM);
        zassert_equal(m5_exchange("127.0.0.1", 42422, kind == SOCK_DGRAM, frame, 200, &metrics),
                      modes[i][2]);
        zassert_equal(metrics.close_error, 0);
        zassert_equal(metrics.owned_after, 0);
        zassert_true(metrics.entered_ms <= metrics.functional_ms &&
                     metrics.functional_ms <= metrics.returned_ms);
        zassert_equal(k_thread_join(&peer, K_SECONDS(2)), 0);
        zassert_equal(m5_fixture_execute(&session, &idle), 0);
    }
    struct net_if *iface = net_if_get_default();
    net_if_carrier_off(iface);
    k_sleep(K_MSEC(100));
    net_if_carrier_on(iface);
    k_sleep(K_MSEC(100));
    zassert_equal(m5_fixture_execute(&session, &idle), 0);
}
ZTEST_SUITE(m5_fixture, NULL, NULL, NULL, NULL, NULL);
