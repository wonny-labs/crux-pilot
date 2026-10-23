/* Private native instrumentation; no declarations are exported to consumers. */
#include "fixture.h"
#include "frame.h"
#include <crux/netif.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#ifndef M5_PEER
#define M5_PEER "192.168.77.1"
#endif
K_THREAD_STACK_DEFINE(m5_observer, 2048);
static struct k_thread observer;
static k_tid_t main_thread;
K_MUTEX_DEFINE(records);
static atomic_t token, phase, observer_failed;
static int64_t stamp(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        return -1;
    return (int64_t)value.tv_sec * 1000 + value.tv_nsec / 1000000;
}
void m5_fixture_record(const char *format, ...) {
    va_list args;
    va_start(args, format);
    k_mutex_lock(&records, K_FOREVER);
    vprintk(format, args);
    k_mutex_unlock(&records);
    va_end(args);
}
static void observe(void *a, void *b, void *c) {
    (void)a;
    (void)b;
    (void)c;
    int previous = -1;
    while (true) {
        uint32_t identity = (uint32_t)atomic_get(&token);
        if (identity != 0) {
            unsigned int sequence = (unsigned int)atomic_get(&phase);
            crux_netif_status_t status;
            int result = crux_netif_get_status(&status);
            int64_t at = stamp();
            if (result != CRUX_OK || at < 0) {
                atomic_set(&observer_failed, 1);
                return;
            }
            if (sequence != (unsigned int)atomic_get(&phase))
                continue;
            if ((int)status.link != previous) {
                previous = (int)status.link;
                uint32_t ip = (uint32_t)status.ipv4_address[0] << 24 |
                              (uint32_t)status.ipv4_address[1] << 16 |
                              (uint32_t)status.ipv4_address[2] << 8 | status.ipv4_address[3];
                /* Timestamp and state precede the record lock; no edge-time guarantee. */
                m5_fixture_record(
                    "M5 LINK token=%u seq=%u at=%lld link=%d admin=%d address=%d ipv4=%u\n",
                    identity, sequence, (long long)at, previous, status.administrative_up,
                    status.ipv4_available, ip);
            }
        }
        k_sleep(K_MSEC(50));
    }
}
static void count_context(struct net_context *context, void *count) {
    if (net_context_is_used(context))
        ++*(unsigned int *)count;
}
struct snapshot {
    int64_t at;
    unsigned int contexts, rx_pkt, tx_pkt;
    size_t rx_buf, tx_buf, main_free, observer_free;
};
static int snapshot(struct snapshot *out) {
    struct k_mem_slab *rx, *tx;
    struct net_buf_pool *rx_data, *tx_data;
    *out = (struct snapshot){.at = stamp()};
    if (out->at < 0 || atomic_get(&observer_failed))
        return -1;
    net_context_foreach(count_context, &out->contexts);
    net_pkt_get_info(&rx, &tx, &rx_data, &tx_data);
    out->rx_pkt = k_mem_slab_num_free_get(rx);
    out->tx_pkt = k_mem_slab_num_free_get(tx);
    out->rx_buf = net_buf_get_available(rx_data);
    out->tx_buf = net_buf_get_available(tx_data);
    if (k_thread_stack_space_get(main_thread, &out->main_free) != 0 ||
        k_thread_stack_space_get(&observer, &out->observer_free) != 0)
        return -1;
    return 0;
}
static bool full(const struct snapshot *s) {
    return s->contexts == 0 && s->rx_pkt == 16 && s->tx_pkt == 16 && s->rx_buf == 32 &&
           s->tx_buf == 32 && s->main_free >= 1024 && s->observer_free >= 512;
}
static int settle(uint32_t identity, unsigned int sequence, unsigned int checkpoint,
                  int64_t returned) {
    struct snapshot saved[3];
    unsigned int count = 0;
    while (stamp() >= 0 && stamp() - returned <= 5000) {
        struct snapshot current;
        if (snapshot(&current) != 0)
            return -1;
        if (!full(&current)) {
            count = 0;
        } else if (count == 0 || current.at - saved[count - 1].at >= 1000) {
            saved[count++] = current;
            if (count == 3) {
                for (unsigned int i = 0; i < 3; ++i) {
                    const struct snapshot *s = &saved[i];
                    m5_fixture_record(
                        "M5 IDLE token=%u seq=%u checkpoint=%u at=%lld contexts=%u rx_pkt=%u "
                        "tx_pkt=%u rx_buf=%zu tx_buf=%zu main_free=%zu observer_free=%zu\n",
                        identity, sequence, checkpoint, (long long)s->at, s->contexts, s->rx_pkt,
                        s->tx_pkt, s->rx_buf, s->tx_buf, s->main_free, s->observer_free);
                }
                return 0;
            }
        }
        k_sleep(K_MSEC(10));
    }
    return -1;
}
int m5_fixture_init(void) {
    if (main_thread != NULL)
        return -1;
    main_thread = k_current_get();
    k_thread_create(&observer, m5_observer, K_THREAD_STACK_SIZEOF(m5_observer), observe, NULL, NULL,
                    NULL, 7, 0, K_NO_WAIT);
    return 0;
}
int m5_fixture_execute(const struct fixture_session *session,
                       const struct fixture_command *command) {
    uint32_t identity = session->token;
    atomic_set(&token, (atomic_val_t)identity);
    atomic_set(&phase, (atomic_val_t)command->sequence);
    if (command->action == 'A') {
        m5_fixture_record("M5 ARM token=%u at=%lld\n", identity, (long long)stamp());
        return 0;
    }
    if (command->action == 'I') {
        if (settle(identity, command->sequence, 0, stamp()) != 0)
            return -1;
    } else {
        int64_t deadline = stamp() + 150000;
        for (unsigned int round = 0; round < command->count; ++round) {
            for (unsigned int udp = 0; udp < 2; ++udp) {
                if (stamp() < 0 || stamp() >= deadline || atomic_get(&observer_failed))
                    return -1;
                uint8_t frame[32];
                struct m5_metrics metrics;
                m5_frame(frame, identity, (uint16_t)command->sequence, round + 1, udp != 0);
                m5_fixture_record("M5 BEGIN token=%u seq=%u round=%u udp=%u at=%lld\n", identity,
                                  command->sequence, round, udp, (long long)stamp());
                int result =
                    m5_exchange(M5_PEER, udp ? 42421 : 42420, udp != 0, frame, 2000, &metrics);
                m5_fixture_record(
                    "M5 END token=%u seq=%u round=%u udp=%u at=%lld result=%d entered=%lld "
                    "functional=%lld close_error=%d peak=%d owned=%d\n",
                    identity, command->sequence, round, udp, (long long)metrics.returned_ms, result,
                    (long long)metrics.entered_ms, (long long)metrics.functional_ms,
                    metrics.close_error, metrics.peak_owned, metrics.owned_after);
                if (metrics.close_error || metrics.owned_after != 0)
                    return -1;
                if (settle(identity, command->sequence, round * 2 + udp, metrics.returned_ms) != 0)
                    return -1;
                if (metrics.returned_ms - metrics.entered_ms > 2500) {
                    m5_fixture_record("M5 REVIEW token=%u seq=%u at=%lld\n", identity,
                                      command->sequence, (long long)stamp());
                    return -1;
                }
                /* A wrong result stops later traffic, while declared faults remain testable. */
                bool valid = command->sequence == 3   ? result == -ETIMEDOUT
                             : command->sequence == 5 ? result == (udp ? 0 : -ECONNRESET)
                             : command->action == 'L'
                                 ? (result == -ETIMEDOUT || result == -ENETDOWN ||
                                    result == -ENETUNREACH || result == -ECONNRESET)
                                 : result == 0;
                if (!valid)
                    return -1;
            }
        }
    }
    m5_fixture_record("M5 DONE token=%u seq=%u at=%lld\n", identity, command->sequence,
                      (long long)stamp());
    return 0;
}
