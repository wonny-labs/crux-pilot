/* SPDX-License-Identifier: Apache-2.0
 *
 * Row C11: what Zephyr's TCP stack does with a failed sys_csrand_get() when it makes an initial
 * sequence number.
 *
 * tcp_init_isn() (subsys/net/ip/tcp.c) keys its RFC 6528 hash with a 16-byte key that it reads from
 * sys_csrand_get() on first use. Unpatched, it ignores a failed read, marks the key as read, and
 * keeps an all-zero key for the rest of the boot. The patch makes both ways to make a sequence
 * number refuse instead:
 *   - passive: a SYN to a listening port gets a RST, and no connection is created,
 *   - active:  net_context_connect() returns the error of the read, and no SYN is sent,
 * and it retries the read on the next attempt, so a read that works again gives a good key.
 *
 * The test is one scenario, in order, because the key is read once: (A) the read fails, a SYN
 * arrives, a connect is attempted; (B) the read works again, a SYN arrives, a connect is attempted.
 * It runs on a dummy interface: the test injects the peer's SYN with net_recv_data() and reads what
 * the stack sends in the device's send function. It uses expectations, not assertions, so that the
 * red run on the unpatched tree reports everything it sees.
 *
 * sys_csrand_get() is Zephyr's own, over a test entropy device (src/test_entropy.c) whose reads
 * fail on request.
 *
 * The packet building follows tests/net/tcp of Zephyr (Apache-2.0, Intel Corporation).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <psa/crypto.h>
#include <zephyr/kernel.h>
#include <zephyr/net/dummy.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/random/random.h>
#include <zephyr/ztest.h>

#include "ipv4.h"
#include "test_entropy.h"

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_ACK 0x10

#define LISTEN_PORT 4242
#define PEER_PORT_A 50001
#define PEER_PORT_B 50002
#define CONNECT_PORT_A 8001
#define CONNECT_PORT_B 8002

static struct net_in_addr my_addr = {{{192, 0, 2, 1}}};
static struct net_in_addr peer_addr = {{{192, 0, 2, 2}}};

/* ---- the injected failure
 * ---------------------------------------------------------------------------------- */

/* sys_csrand_get() is Zephyr's (CONFIG_HARDWARE_DEVICE_CS_GENERATOR) over the test entropy device
 * of src/test_entropy.c: a failed read there is a failed sys_csrand_get() with -EIO, as on the
 * board's TRNG.
 */
#define csrand_calls crux_test_entropy_reads()

/* ---- the dummy interface and the capture of what the stack sends
 * -------------------------------------------- */

struct seen {
    bool valid;
    uint8_t flags;
    uint32_t seq;
    uint16_t sport; /* as in the packet */
    uint16_t dport;
};

static struct net_if *iface;
static struct seen last_sent;
static unsigned int sent_count;
static K_SEM_DEFINE(sent_sem, 0, 16);

static int tester_send(const struct device *dev, struct net_pkt *pkt)
{
    struct net_tcp_hdr th;

    ARG_UNUSED(dev);
    if (net_pkt_family(pkt) != NET_AF_INET || NET_IPV4_HDR(pkt)->proto != NET_IPPROTO_TCP) {
        return 0;
    }
    net_pkt_cursor_init(pkt);
    net_pkt_set_overwrite(pkt, true);
    if (net_pkt_skip(pkt, net_pkt_ip_hdr_len(pkt) + net_pkt_ip_opts_len(pkt)) < 0 ||
        net_pkt_read(pkt, &th, sizeof(th)) < 0) {
        return -EINVAL;
    }
    net_pkt_cursor_init(pkt);

    last_sent.valid = true;
    last_sent.flags = th.flags;
    last_sent.seq = sys_get_be32(th.seq);
    last_sent.sport = th.src_port;
    last_sent.dport = th.dst_port;
    sent_count++;
    k_sem_give(&sent_sem);
    return 0;
}

static int dev_init(const struct device *dev)
{
    ARG_UNUSED(dev);
    return 0;
}

static void iface_init(struct net_if *net_if)
{
    static uint8_t mac[6] = {0x00, 0x00, 0x5E, 0x00, 0x53, 0x01};

    net_if_set_link_addr(net_if, mac, sizeof(mac), NET_LINK_ETHERNET);
}

static struct dummy_api tester_api = {
    .iface_api.init = iface_init,
    .send = tester_send,
};

NET_DEVICE_INIT(crux_isn_test, "crux_isn_test", dev_init, NULL, NULL, NULL,
                CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tester_api, DUMMY_L2,
                NET_L2_GET_CTX_TYPE(DUMMY_L2), 127);

/* A SYN from the peer to the listening port. */
static int inject_syn(uint16_t peer_port)
{
    NET_PKT_DATA_ACCESS_DEFINE(access, struct net_tcp_hdr);
    struct net_tcp_hdr *th;
    struct net_pkt *pkt;
    int ret;

    pkt = net_pkt_alloc_with_buffer(iface, sizeof(struct net_tcp_hdr), NET_AF_INET, NET_IPPROTO_TCP,
                                    K_NO_WAIT);
    if (pkt == NULL) {
        return -ENOMEM;
    }
    ret = net_ipv4_create(pkt, &peer_addr, &my_addr);
    th = ret < 0 ? NULL : (struct net_tcp_hdr *)net_pkt_get_data(pkt, &access);
    if (th == NULL) {
        net_pkt_unref(pkt);
        return -EINVAL;
    }
    memset(th, 0, sizeof(*th));
    th->src_port = net_htons(peer_port);
    th->dst_port = net_htons(LISTEN_PORT);
    th->offset = 5U << 4;
    th->flags = TCP_SYN;
    sys_put_be32(1000U, th->seq);
    sys_put_be16(1280U, th->wnd);
    if (net_pkt_set_data(pkt, &access) < 0) {
        net_pkt_unref(pkt);
        return -EINVAL;
    }
    net_pkt_cursor_init(pkt);
    if (net_ipv4_finalize(pkt, NET_IPPROTO_TCP) < 0) {
        net_pkt_unref(pkt);
        return -EINVAL;
    }
    ret = net_recv_data(iface, pkt);
    if (ret < 0) {
        net_pkt_unref(pkt);
    }
    return ret;
}

/* ---- what a key would give
 * -------------------------------------------------------------------------------- */

/* The key that the test entropy device gives to a read of 16 bytes (src/test_entropy.c). */
static void device_key(uint8_t key[16])
{
    for (unsigned int index = 0; index < 16U; ++index) {
        key[index] = (uint8_t)(0xA5U ^ (index * 37U));
    }
}

/* The hash of tcpv4_init_isn() (subsys/net/ip/tcp.c) for this key, these addresses and these ports
 * (as the stack stores them: the ports in network order). The stack then adds the uptime, in 64 ns
 * steps, in seq_scale().
 */
static uint32_t key_hash(const uint8_t key[16], const struct net_in_addr *saddr,
                         const struct net_in_addr *daddr, uint16_t sport, uint16_t dport)
{
    struct {
        uint8_t key[16];
        struct net_in_addr saddr;
        struct net_in_addr daddr;
        uint16_t sport;
        uint16_t dport;
    } buf = {.saddr = *saddr, .daddr = *daddr, .sport = sport, .dport = dport};
    /* A SHA-256 digest is 32 bytes. */
    uint8_t hash[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t length = 0;
    uint32_t word;
    psa_status_t status;

    memcpy(buf.key, key, sizeof(buf.key));
    status = psa_hash_compute(PSA_ALG_SHA_256, (const uint8_t *)&buf, sizeof(buf), hash,
                              sizeof(hash), &length);
    zassert_equal(status, PSA_SUCCESS, "psa_hash_compute: %d", (int)status);
    memcpy(&word, hash, sizeof(word));
    return word;
}

static uint32_t uptime_scale(void)
{
    return k_ticks_to_ns_floor32(k_uptime_ticks()) >> 6;
}

/* Whether isn is the hash plus an uptime between the two readings. */
static bool is_hash_plus_uptime(uint32_t isn, uint32_t hash, uint32_t before, uint32_t after)
{
    uint32_t delta = isn - hash;

    /* The uptime counter wraps after about 4.3 s of 64 ns steps: a window that wraps is accepted on
     * either side. */
    return before <= after ? (delta >= before && delta <= after)
                           : (delta >= before || delta <= after);
}

/* ---- the scenario
 * ------------------------------------------------------------------------------------------- */

enum isn_kind {
    ISN_NONE,  /* nothing was sent */
    ISN_KEYED, /* hash of the key that the device gave, plus the uptime: what the stack is meant to
                  send */
    ISN_ZERO_KEY, /* hash of the all-zero key, plus the uptime */
    ISN_UNKEYED,  /* neither: not made from any key */
};

struct outcome {
    bool replied;
    uint8_t flags;
    uint32_t isn;
    enum isn_kind kind;
};

/* sport is the port as the stack stores it. For a connection that a SYN opened, tcp_conn_new()
 * hashes with the port that net_context_get() chose at random for the new context, which
 * net_context_bind() copies back into tcp_conn_new()'s address when its port is 0: the test cannot
 * know it, so any_sport tries every port.
 */
static bool made_from(const uint8_t key[16], uint32_t isn, const struct net_in_addr *saddr,
                      const struct net_in_addr *daddr, uint16_t sport, bool any_sport,
                      uint16_t dport, uint32_t before, uint32_t after)
{
    if (!any_sport) {
        return is_hash_plus_uptime(isn, key_hash(key, saddr, daddr, sport, dport), before, after);
    }
    for (uint32_t port = 0; port <= UINT16_MAX; ++port) {
        if (is_hash_plus_uptime(isn, key_hash(key, saddr, daddr, (uint16_t)port, dport), before,
                                after)) {
            return true;
        }
    }
    return false;
}

static enum isn_kind classify(uint32_t isn, const struct net_in_addr *saddr,
                              const struct net_in_addr *daddr, uint16_t sport, bool any_sport,
                              uint16_t dport, uint32_t before, uint32_t after)
{
    uint8_t key[16];
    uint8_t zero[16] = {0};

    device_key(key);
    if (made_from(key, isn, saddr, daddr, sport, any_sport, dport, before, after)) {
        return ISN_KEYED;
    }
    if (made_from(zero, isn, saddr, daddr, sport, any_sport, dport, before, after)) {
        return ISN_ZERO_KEY;
    }
    return ISN_UNKEYED;
}

static const char *flag_name(uint8_t flags)
{
    if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
        return "SYN|ACK";
    } else if ((flags & TCP_SYN) != 0) {
        return "SYN";
    } else if ((flags & TCP_RST) != 0) {
        return (flags & TCP_ACK) != 0 ? "RST|ACK" : "RST";
    }
    return "other";
}

static unsigned int accepted;

static void accept_cb(struct net_context *context, struct net_sockaddr *addr, net_socklen_t addrlen,
                      int status, void *user_data)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(addrlen);
    ARG_UNUSED(status);
    ARG_UNUSED(user_data);
    accepted++;
    net_context_put(context);
}

/* A SYN to the listening port: what did the stack send back? */
static struct outcome passive(uint16_t peer_port)
{
    struct outcome out = {0};
    uint32_t before;
    uint32_t after;

    k_sem_reset(&sent_sem);
    last_sent.valid = false;
    before = uptime_scale();
    zassert_ok(inject_syn(peer_port), "inject the SYN");
    if (k_sem_take(&sent_sem, K_MSEC(300)) == 0) {
        after = uptime_scale();
        out.replied = true;
        out.flags = last_sent.flags;
        out.isn = last_sent.seq;
        /* tcp_conn_new() hashes with its own local address and its new context's port, the peer's
         * address and port. */
        out.kind = (out.flags & TCP_SYN) == 0 ? ISN_NONE
                                              : classify(out.isn, &my_addr, &peer_addr, 0, true,
                                                         net_htons(peer_port), before, after);
    }
    return out;
}

/* A connect to the peer: did the stack send a SYN, and what did connect return? */
static struct outcome active(uint16_t port, int *connect_result)
{
    struct net_sockaddr_in peer = {
        .sin_family = NET_AF_INET, .sin_port = net_htons(port), .sin_addr = peer_addr};
    struct outcome out = {0};
    struct net_context *context;
    uint32_t before;
    uint32_t after;

    zassert_ok(net_context_get(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP, &context),
               "net_context_get");
    k_sem_reset(&sent_sem);
    last_sent.valid = false;
    before = uptime_scale();
    *connect_result = net_context_connect(context, (struct net_sockaddr *)&peer, sizeof(peer), NULL,
                                          K_NO_WAIT, NULL);
    if (k_sem_take(&sent_sem, K_MSEC(300)) == 0) {
        after = uptime_scale();
        out.replied = true;
        out.flags = last_sent.flags;
        out.isn = last_sent.seq;
        out.kind = (out.flags & TCP_SYN) == 0
                       ? ISN_NONE
                       : classify(out.isn, &my_addr, &peer_addr, last_sent.sport, false,
                                  last_sent.dport, before, after);
    }
    net_context_put(context);
    k_sleep(K_MSEC(50));
    return out;
}

static const char *kind_name(const struct outcome *out)
{
    switch (out->kind) {
    case ISN_KEYED:
        return "keyed";
    case ISN_ZERO_KEY:
        return "zero-key";
    case ISN_UNKEYED:
        return "unkeyed";
    default:
        return "-";
    }
}

static void report(const char *phase, const char *path, const struct outcome *out,
                   const int *connect_result)
{
    printf("ISN-REPORT phase=%s path=%s sent=%s isn=%s connect=", phase, path,
           out->replied ? flag_name(out->flags) : "nothing", kind_name(out));
    if (connect_result != NULL) {
        printf("%d\n", *connect_result);
    } else {
        printf("-\n");
    }
}

ZTEST(isn_fail_closed, test_scenario)
{
    struct net_sockaddr_in listen_addr = {
        .sin_family = NET_AF_INET, .sin_port = net_htons(LISTEN_PORT), .sin_addr = my_addr};
    struct net_context *listener;
    struct outcome out;
    int connect_result;

    zassert_ok(net_context_get(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP, &listener),
               "listener");
    zassert_ok(net_context_bind(listener, (struct net_sockaddr *)&listen_addr, sizeof(listen_addr)),
               "bind");
    zassert_ok(net_context_listen(listener, 2), "listen");
    zassert_ok(net_context_accept(listener, accept_cb, K_NO_WAIT, NULL), "accept");

    /* (A) The read fails. */
    crux_test_entropy_result = -EIO;
    out = passive(PEER_PORT_A);
    report("A-read-fails", "passive", &out, NULL);
    zexpect_true(out.replied, "A passive: the stack must answer the SYN");
    zexpect_true(
        out.replied && (out.flags & TCP_RST) != 0 && (out.flags & TCP_SYN) == 0,
        "A passive: the SYN must be refused with a RST, not answered with a SYN|ACK (sent %s)",
        out.replied ? flag_name(out.flags) : "nothing");
    zexpect_equal(out.kind, ISN_NONE,
                  "A passive: no initial sequence number without a key (got %s)", kind_name(&out));
    zexpect_equal(accepted, 0U, "A passive: no connection must be accepted");

    out = active(CONNECT_PORT_A, &connect_result);
    report("A-read-fails", "active", &out, &connect_result);
    zexpect_true(connect_result < 0 && connect_result != -EINPROGRESS,
                 "A active: net_context_connect() must return the error of the read, not %d",
                 connect_result);
    zexpect_false(out.replied, "A active: no SYN must be sent (sent %s)",
                  out.replied ? flag_name(out.flags) : "nothing");
    zexpect_equal(out.kind, ISN_NONE, "A active: no initial sequence number without a key (got %s)",
                  kind_name(&out));

    /* (B) The read works again. The next attempt must read the key again and get a good one. */
    crux_test_entropy_result = 0;
    out = passive(PEER_PORT_B);
    report("B-read-works", "passive", &out, NULL);
    zexpect_true(out.replied && (out.flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK),
                 "B passive: the SYN must be answered with a SYN|ACK (sent %s)",
                 out.replied ? flag_name(out.flags) : "nothing");
    zexpect_equal(
        out.kind, ISN_KEYED,
        "B passive: the initial sequence number must be made from the key that was read (got %s)",
        kind_name(&out));

    out = active(CONNECT_PORT_B, &connect_result);
    report("B-read-works", "active", &out, &connect_result);
    zexpect_true(connect_result == -EINPROGRESS || connect_result == 0, "B active: connect gave %d",
                 connect_result);
    zexpect_true(out.replied && (out.flags & TCP_SYN) != 0, "B active: a SYN must be sent");
    zexpect_equal(
        out.kind, ISN_KEYED,
        "B active: the initial sequence number must be made from the key that was read (got %s)",
        kind_name(&out));

    printf("ISN-REPORT entropy-reads=%u accepted=%u\n", csrand_calls, accepted);
}

static void *suite_setup(void)
{
    static struct net_in_addr addr_copy;

    /* The boot-time initialisation may have run before the entropy device was ready. The device
     * works here. */
    zassert_equal(psa_crypto_init(), PSA_SUCCESS, "psa_crypto_init");
    iface = net_if_get_first_by_type(&NET_L2_GET_NAME(DUMMY));
    zassert_not_null(iface, "interface");
    addr_copy = my_addr;
    zassert_not_null(net_if_ipv4_addr_add(iface, &addr_copy, NET_ADDR_MANUAL, 0), "address");
    return NULL;
}

ZTEST_SUITE(isn_fail_closed, NULL, suite_setup, NULL, NULL, NULL);
