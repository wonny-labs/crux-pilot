/*
 * The bench image's report: the glue between the generic report (../report/bench_report_core.c) and
 * what only this image has. TCP segments of the one interface tell the report that a client is
 * talking (no hook in any component), the SSH provider keeps the figure of the workers that ended,
 * and the certificate module has the fingerprint. See bench_report.h.
 */
#include "bench_guard.h"

#include "bench_report.h"

#include "bench_report_core.h"
#include "bench_report_format.h"

#include "crux_ssh_wolf_provider.h"

#include <crux_tls_certificate_adapter.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_pkt.h>
#include <zephyr/sys/printk.h>

#if !defined(CONFIG_NET_STATISTICS_PER_INTERFACE) || !defined(CONFIG_NET_STATISTICS_TCP)
#error                                                                                             \
    "The bench report counts TCP segments per interface: NET_STATISTICS_PER_INTERFACE and NET_STATISTICS_TCP"
#endif
#if !defined(CONFIG_NET_BUF_POOL_USAGE) || !defined(CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION) ||      \
    !defined(CONFIG_NET_STATISTICS_IPV4)
#error                                                                                             \
    "The bench report reads the network pools' high-water marks and the IPv4 counters (row C6): NET_BUF_POOL_USAGE, MEM_SLAB_TRACE_MAX_UTILIZATION and NET_STATISTICS_IPV4"
#endif
#if !defined(CONFIG_CRUX_SSH_STACK_REPORT)
#error "The bench report reads the SSH workers' stacks: CONFIG_CRUX_SSH_STACK_REPORT"
#endif

/* The segments received and sent on the one interface. Wraps at 2^32, which the policy allows. */
static uint32_t tcp_segments(void)
{
    struct net_if *iface = net_if_get_default();

    if (iface == NULL) {
        return 0u;
    }
    return (uint32_t)iface->stats.tcp.recv + (uint32_t)iface->stats.tcp.sent;
}

/* Row C6. The network stack's packets are memory slabs and its data buffers are net_buf pools. Both
 * keep the most that was ever in use: the pools' own high-water marks, which no component needs to
 * maintain. The size is the constant that each was built with, so that a read in the middle of an
 * allocation cannot make it wrong. */
static void slab_row(bench_report_pool_t *row, const char *name, struct k_mem_slab *slab)
{
    uint32_t used = k_mem_slab_num_used_get(slab);

    row->name = name;
    row->size = slab->info.num_blocks;
    row->used = MIN(used, row->size);
    row->peak = MIN(MAX(k_mem_slab_max_used_get(slab), row->used), row->size);
}

static void buf_row(bench_report_pool_t *row, const char *name, struct net_buf_pool *pool)
{
    uint32_t available = (uint32_t)net_buf_get_available(pool);

    row->name = name;
    row->size = pool->pool_size;
    row->used = available >= row->size ? 0u : row->size - available;
    row->peak = MIN(MAX((uint32_t)net_buf_get_max_used(pool), row->used), row->size);
}

static bool net_snapshot(bench_report_net_t *out)
{
    struct net_if *iface = net_if_get_default();
    struct k_mem_slab *rx_pkt = NULL;
    struct k_mem_slab *tx_pkt = NULL;
    struct net_buf_pool *rx_buf = NULL;
    struct net_buf_pool *tx_buf = NULL;

    if (iface == NULL) {
        return false;
    }
    net_pkt_get_info(&rx_pkt, &tx_pkt, &rx_buf, &tx_buf);
    slab_row(&out->pools[0], "rx_pkt", rx_pkt);
    slab_row(&out->pools[1], "tx_pkt", tx_pkt);
    buf_row(&out->pools[2], "rx_buf", rx_buf);
    buf_row(&out->pools[3], "tx_buf", tx_buf);
    out->pool_count = 4u;
    /* The ENET driver counts no receive drop (it has no statistics callback), so the pools' marks
     * are what shows a drop for want of a packet or a buffer. These are the counters that exist. */
    out->ip_drop = (uint32_t)iface->stats.ipv4.drop;
    out->tcp_drop = (uint32_t)iface->stats.tcp.drop;
    out->tcp_seg_drop = (uint32_t)iface->stats.tcp.seg_drop;
    out->tcp_rexmit = (uint32_t)iface->stats.tcp.rexmit;
    out->tcp_conndrop = (uint32_t)iface->stats.tcp.conndrop;
    return true;
}

/* Before each snapshot: the workers that have ended are in no thread list, so the provider's
 * figure is noted as a row of its own. The live workers are in the list under the same name. */
static void note_ended_threads(void)
{
    size_t size = 0;
    size_t unused = 0;

    if (crux_ssh_wolf_worker_stack_ended(&size, &unused)) {
        (void)bench_report_core_note_ended("ssh_worker", (uint32_t)size, (uint32_t)unused, false);
    }
}

bool crux_bench_report_start(void)
{
    static const bench_report_core_config_t config = {
        .activity = tcp_segments,
        .refresh = note_ended_threads,
        .arena_bytes = CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE,
        .net = net_snapshot,
    };
    return bench_report_core_start(&config);
}

void crux_bench_report_main_exit(void)
{
    size_t unused = 0;
    struct k_thread *self = k_current_get();

    if (k_thread_stack_space_get(self, &unused) == 0) {
        (void)bench_report_core_note_ended("main", (uint32_t)self->stack_info.size,
                                           (uint32_t)unused, true);
    }
}

bool crux_bench_report_certificate(bool generated)
{
    char fingerprint[CRUX_TLS_CERTIFICATE_FINGERPRINT_TEXT_SIZE];
    char line[BENCH_REPORT_LINE_MAX];
    crux_tls_certificate_result_t result = CRUX_TLS_CERTIFICATE_ERR_BUSY;

    /* BUSY means another operation holds the module for a moment. */
    for (int attempt = 0; attempt < 10 && result == CRUX_TLS_CERTIFICATE_ERR_BUSY; attempt++) {
        result = crux_tls_certificate_fingerprint(fingerprint);
        if (result == CRUX_TLS_CERTIFICATE_ERR_BUSY) {
            k_msleep(20);
        }
    }
    if (result != CRUX_TLS_CERTIFICATE_OK ||
        bench_report_format_cert(line, sizeof(line), fingerprint, generated) == 0u) {
        printk("BENCH-CERT unavailable\n");
        return false;
    }
    printk("%s\n", line);
    return true;
}
