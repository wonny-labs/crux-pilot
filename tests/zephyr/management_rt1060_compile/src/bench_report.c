/*
 * The bench image's report: the glue between the generic report (../report/bench_report_core.c) and
 * what only this image has. TCP segments of the one interface tell the report that a client is talking
 * (no hook in any component), the SSH provider keeps the figure of the workers that ended, and the
 * certificate module has the fingerprint. See bench_report.h.
 */
#include "bench_guard.h"

#include "bench_report.h"

#include "bench_report_core.h"
#include "bench_report_format.h"

#include "crux_ssh_wolf_provider.h"

#include <crux_tls_certificate_adapter.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/printk.h>

#if !defined(CONFIG_NET_STATISTICS_PER_INTERFACE) || !defined(CONFIG_NET_STATISTICS_TCP)
#error "The bench report counts TCP segments per interface: NET_STATISTICS_PER_INTERFACE and NET_STATISTICS_TCP"
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
