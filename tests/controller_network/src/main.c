#include <zephyr/kernel.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>

static enum {
    ECHO,
    CONNECT_FAIL,
    CONNECT_ERROR,
    EOF_REPLY,
    OVERSIZE_REPLY,
    WRONG_REPLY,
    POLL_INTERRUPT,
    SEND_INTERRUPT,
    SEND_AGAIN,
    CLOSE_FAIL
} scenario;
static int64_t elapsed;
static unsigned int creates;
static unsigned int closes;
static unsigned int sends;
static size_t transmitted;
static size_t received;
static uint8_t saved[32];
static unsigned int context_count;
static bool release_context;

static void test_contexts(net_context_cb_t callback, void *data) {
    struct net_context context = {0};
    for (unsigned int i = 0; i < context_count; ++i) {
        callback(&context, data);
    }
}

static bool test_used(struct net_context *context) {
    ARG_UNUSED(context);
    return true;
}

static int32_t test_sleep(k_timeout_t duration) {
    elapsed += k_ticks_to_ms_floor64(duration.ticks);
    if (release_context && elapsed >= 100) {
        context_count = 0;
    }
    return 0;
}

static enum { INIT_OK, NO_INTERFACE, ADDRESS_FAIL, MASK_FAIL, UP_FAIL } init_scenario;
static bool reported_link;
static struct net_if *actual_default(void) {
    return net_if_get_default();
}
static struct net_if *test_default(void) {
    return init_scenario == NO_INTERFACE ? NULL : actual_default();
}
static struct net_if_addr *test_add_address(struct net_if *iface, const struct net_in_addr *addr,
                                            enum net_addr_type type, uint32_t lifetime) {
    ARG_UNUSED(iface);
    ARG_UNUSED(addr);
    ARG_UNUSED(type);
    ARG_UNUSED(lifetime);
    static struct net_if_addr result;
    return init_scenario == ADDRESS_FAIL ? NULL : &result;
}
static bool test_mask(struct net_if *iface, const struct net_in_addr *addr,
                      const struct net_in_addr *mask) {
    ARG_UNUSED(iface);
    ARG_UNUSED(addr);
    ARG_UNUSED(mask);
    return init_scenario != MASK_FAIL;
}
static int test_up(struct net_if *iface) {
    ARG_UNUSED(iface);
    return init_scenario == UP_FAIL ? -EIO : -EALREADY;
}
static bool test_carrier(struct net_if *iface) {
    ARG_UNUSED(iface);
    return reported_link;
}

static int64_t test_now(void) {
    return elapsed;
}
static int test_socket(int family, int type, int protocol) {
    ARG_UNUSED(family);
    ARG_UNUSED(type);
    ARG_UNUSED(protocol);
    creates++;
    transmitted = received = sends = 0;
    return 7;
}
static int test_flags(int fd, int command, int flags) {
    zassert_equal(fd, 7);
    zassert_equal(command, F_SETFL);
    zassert_equal(flags, O_NONBLOCK);
    return 0;
}
static int test_connect(int fd, const struct sockaddr *address, socklen_t length) {
    ARG_UNUSED(fd);
    ARG_UNUSED(address);
    ARG_UNUSED(length);
    if (scenario == CONNECT_FAIL) {
        errno = ECONNREFUSED;
        return -1;
    }
    if (scenario == CONNECT_ERROR) {
        errno = EINPROGRESS;
        return -1;
    }
    return 0;
}
static int test_option(int fd, int level, int option, void *value, socklen_t *length) {
    ARG_UNUSED(fd);
    ARG_UNUSED(level);
    ARG_UNUSED(option);
    ARG_UNUSED(length);
    *(int *)value = ECONNREFUSED;
    return 0;
}
static int test_poll(struct zsock_pollfd *items, int count, int timeout) {
    zassert_equal(count, 1);
    zassert_true(timeout > 0 && timeout <= 2000);
    elapsed += MIN(100, timeout);
    if (scenario == POLL_INTERRUPT) {
        errno = EINTR;
        return -1;
    }
    items->revents = items->events;
    return 1;
}
static ssize_t test_send(int fd, const void *data, size_t length, int flags) {
    ARG_UNUSED(fd);
    ARG_UNUSED(flags);
    sends++;
    if (sends == 1 && (scenario == SEND_INTERRUPT || scenario == SEND_AGAIN)) {
        errno = scenario == SEND_INTERRUPT ? EINTR : EAGAIN;
        return -1;
    }
    size_t part = length > 5 ? 5 : length;
    /* TCP intentionally fragments; a datagram is emitted in one operation. */
    if (length == 32 && ((const uint8_t *)data)[7] == 2) {
        part = length;
    }
    memcpy(saved + transmitted, data, part);
    transmitted += part;
    return (ssize_t)part;
}
static ssize_t test_receive(int fd, void *data, size_t length, int flags) {
    ARG_UNUSED(fd);
    ARG_UNUSED(flags);
    if (scenario == EOF_REPLY) {
        return 0;
    }
    if (scenario == OVERSIZE_REPLY) {
        memset(data, 0, length);
        return (ssize_t)length;
    }
    size_t part = length > 3 ? 3 : length;
    if (saved[7] == 2) {
        part = 32;
    }
    memcpy(data, saved + received, part);
    if (scenario == WRONG_REPLY) {
        ((uint8_t *)data)[0] ^= 1;
    }
    received += part;
    return (ssize_t)part;
}
static int test_close(int fd) {
    ARG_UNUSED(fd);
    closes++;
    if (scenario == CLOSE_FAIL) {
        errno = EIO;
        return -1;
    }
    return 0;
}

/* Instrument the actual native client without publishing production test hooks. */
#undef zsock_fcntl
#define zsock_socket test_socket
#define zsock_fcntl test_flags
#define zsock_connect test_connect
#define zsock_getsockopt test_option
#define zsock_poll test_poll
#define zsock_send test_send
#define zsock_recv test_receive
#define zsock_close test_close
#define k_uptime_get test_now
#define k_sleep test_sleep
#define net_context_foreach test_contexts
#define net_context_is_used test_used
#define net_if_get_default test_default
#define net_if_ipv4_addr_add test_add_address
#define net_if_ipv4_set_netmask_by_addr test_mask
#define net_if_up test_up
#define net_if_is_carrier_ok test_carrier
#include "../../../samples/controller/zephyr/network_client.c"

static void before(void *fixture) {
    ARG_UNUSED(fixture);
    scenario = ECHO;
    elapsed = 0;
    creates = closes = sends = 0;
    transmitted = received = 0;
    memset(saved, 0, sizeof(saved));
    owned_sockets = 0;
    context_count = idle_contexts = 0;
    baseline_valid = true;
    cleanup_uncertain = release_context = false;
    address_ready = true;
    service_ready = false;
    init_scenario = INIT_OK;
    reported_link = true;
    interface = actual_default();
}

ZTEST(network, test_cleanup_waits_and_uncertain_state_fences_restart) {
    context_count = 1;
    release_context = true;
    zassert_ok(settle_contexts(150000));
    zassert_equal(elapsed, 100);
    zassert_false(cleanup_uncertain);
    release_context = false;
    context_count = 1;
    zassert_equal(settle_contexts(150000), -ETIMEDOUT);
    zassert_equal(elapsed, 5100);
    zassert_true(cleanup_uncertain);
    zassert_equal(controller_network_round(1), -EBUSY);
    zassert_equal(controller_network_init(), -EBUSY);
    zassert_equal(creates, 0);
}

ZTEST(network, test_cleanup_never_rebases_or_extends_round_deadline) {
    context_count = 1;
    zassert_equal(controller_network_init(), -EBUSY);
    zassert_equal(idle_contexts, 0);
    cleanup_uncertain = false;
    zassert_equal(settle_contexts(30), -ETIMEDOUT);
    zassert_equal(elapsed, 30);
    zassert_true(cleanup_uncertain);
}

ZTEST(network, test_expired_and_near_end_round_budget) {
    zassert_equal(exchange(1, 1, 1, 0), -ETIMEDOUT);
    zassert_equal(creates, 0);
    elapsed = 149999;
    scenario = POLL_INTERRUPT;
    zassert_equal(exchange(1, 1, 1, 150000), -ETIMEDOUT);
    zassert_equal(elapsed, 150000);
    zassert_equal(creates, 1);
    zassert_equal(closes, 1);
    zassert_equal(settle_contexts(150000), -ETIMEDOUT);
    zassert_true(cleanup_uncertain);
}

ZTEST(network, test_partial_tcp_and_datagram_echo) {
    zassert_ok(exchange(1, 1, 1, elapsed + 2000));
    zassert_equal(transmitted, 32);
    zassert_equal(received, 32);
    zassert_equal(owned_sockets, 0);
    zassert_equal(closes, 1);
    transmitted = received = 0;
    zassert_ok(exchange(2, 1, 1, elapsed + 2000));
    zassert_equal(transmitted, 32);
    zassert_equal(received, 32);
}

ZTEST(network, test_connect_and_stream_failures_close_once) {
    scenario = CONNECT_FAIL;
    zassert_equal(exchange(1, 1, 1, elapsed + 2000), -ECONNREFUSED);
    zassert_equal(closes, 1);
    scenario = CONNECT_ERROR;
    zassert_equal(exchange(1, 1, 1, elapsed + 2000), -ECONNREFUSED);
    zassert_equal(closes, 2);
    scenario = EOF_REPLY;
    zassert_equal(exchange(1, 1, 1, elapsed + 2000), -ECONNRESET);
    zassert_equal(closes, 3);
    zassert_equal(owned_sockets, 0);
}

ZTEST(network, test_udp_oversize_and_wrong_payload_rejected) {
    scenario = OVERSIZE_REPLY;
    zassert_equal(exchange(2, 1, 1, elapsed + 2000), -EMSGSIZE);
    transmitted = received = 0;
    scenario = WRONG_REPLY;
    zassert_equal(exchange(2, 1, 1, elapsed + 2000), -EBADMSG);
    zassert_equal(owned_sockets, 0);
}

ZTEST(network, test_repeated_interrupts_cannot_extend_deadline) {
    scenario = POLL_INTERRUPT;
    zassert_equal(exchange(1, 1, 1, elapsed + 2000), -ETIMEDOUT);
    zassert_equal(elapsed, 2000);
    zassert_equal(owned_sockets, 0);
    zassert_equal(closes, 1);
}

ZTEST(network, test_interrupted_and_would_block_stream_send_recovers) {
    scenario = SEND_INTERRUPT;
    zassert_ok(exchange(1, 1, 1, elapsed + 2000));
    transmitted = received = sends = 0;
    elapsed = 0;
    scenario = SEND_AGAIN;
    zassert_ok(exchange(1, 1, 1, elapsed + 2000));
    zassert_equal(owned_sockets, 0);
}

ZTEST(network, test_failed_close_prevents_further_round_creation) {
    scenario = CLOSE_FAIL;
    zassert_equal(exchange(2, 1, 1, elapsed + 2000), -EIO);
    zassert_equal(owned_sockets, 1);
    zassert_equal(controller_network_round(2), -EBUSY);
    zassert_equal(creates, 1);
}

ZTEST(network, test_init_failure_never_claims_address_or_service_ready) {
    for (int failure = NO_INTERFACE; failure <= UP_FAIL; ++failure) {
        init_scenario = failure;
        zassert_true(controller_network_init() < 0);
        zassert_false(address_ready);
        zassert_false(service_ready);
        zassert_equal(controller_network_round(1), -EINVAL);
        zassert_equal(creates, 0);
    }
    init_scenario = INIT_OK;
    zassert_ok(controller_network_init());
    zassert_true(address_ready);
    zassert_false(service_ready);
}

ZTEST(network, test_readiness_failure_link_loss_and_explicit_recovery) {
    zassert_ok(controller_network_round(1));
    zassert_true(service_ready);
    reported_link = false;
    controller_network_report();
    zassert_false(service_ready);
    reported_link = true;
    controller_network_report();
    zassert_false(service_ready);
    zassert_ok(controller_network_round(2));
    zassert_true(service_ready);
    scenario = CONNECT_FAIL;
    zassert_equal(controller_network_round(3), -EIO);
    zassert_false(service_ready);
    scenario = ECHO;
    zassert_ok(controller_network_round(4));
    zassert_true(service_ready);
    zassert_equal(owned_sockets, 0);
}

ZTEST(network, test_full_pair_reuses_cleanup_and_failed_tcp_never_claims_pair_progress) {
    zassert_ok(controller_network_pair(1, 1, 14000));
    zassert_equal(creates, 2);
    zassert_equal(closes, 2);
    zassert_true(service_ready);
    scenario = CONNECT_FAIL;
    zassert_equal(controller_network_pair(1, 2, elapsed + 14000), -ECONNREFUSED);
    zassert_equal(creates, 3);
    zassert_false(service_ready);
    zassert_equal(owned_sockets, 0);
    scenario = ECHO;
    zassert_ok(controller_network_pair(1, 3, elapsed + 14000));
    zassert_true(service_ready);
    context_count = 1;
    zassert_equal(controller_network_pair(1, 4, elapsed + 14000), -ETIMEDOUT);
    zassert_true(cleanup_uncertain);
    unsigned int before_creates = creates;
    zassert_equal(controller_network_pair(1, 5, elapsed + 14000), -EBUSY);
    zassert_equal(creates, before_creates);
}

ZTEST(network, test_pair_deadline_and_invalid_inputs_cannot_create_socket) {
    zassert_equal(controller_network_pair(0, 1, 14000), -EINVAL);
    zassert_equal(controller_network_pair(1, 0, 14000), -EINVAL);
    zassert_equal(controller_network_pair(1, 1, 0), -ETIMEDOUT);
    zassert_equal(creates, 0);
    zassert_true(cleanup_uncertain);
    zassert_equal(controller_network_pair(1, 2, 14000), -EBUSY);
    zassert_equal(creates, 0);
}

ZTEST_SUITE(network, NULL, NULL, before, NULL, NULL);
