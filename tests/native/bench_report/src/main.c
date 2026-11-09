/*
 * Scenario for the bench report's kernel side (report/bench_report_core.c) on native_sim.
 *
 *   t = 0        the report starts: a boot report with the fixture threads
 *   t = 10 s     a "client": the activity counter moves for half a second
 *   t = 12.5 s   a quiet report, with the ended rows (main, ssh_worker) that the fixture noted
 *   t = 40.5 s   one idle report
 *   t = 46 s     a table overflow on purpose: 60 more threads, so the report must say missed=N and
 * stop at the table's size and not print past it
 *
 * The network lines (row C6) come from a fake read of made-up pools and counters; its third read
 * fails.
 *
 * The output is the bench image's own lines. tests/build/test_management_bench_report.py documents
 * how the capture is fed to check_bench_uart.py. The stack numbers are native_sim's and mean
 * nothing.
 */
#include "bench_report_core.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define HTTP_STACK_BYTES 8192
#define EXTRA_THREADS 60u

static volatile uint32_t activity;
static K_THREAD_STACK_DEFINE(http_stack, HTTP_STACK_BYTES);
static struct k_thread http_thread;
static K_THREAD_STACK_DEFINE(anonymous_stack, 1024);
static struct k_thread anonymous_thread;
static K_THREAD_STACK_ARRAY_DEFINE(extra_stacks, EXTRA_THREADS, 512);
static struct k_thread extra_threads[EXTRA_THREADS];

static uint32_t read_activity(void)
{
    return activity;
}

/* The network side of the report (row C6) with made-up figures: the image's own callback reads the
 * network stack, which native_sim does not have in this test. The third snapshot's read fails on
 * purpose, so that the report must leave the pool and counter lines out and say so in its line
 * count. */
static bool fake_net(bench_report_net_t *out)
{
    static unsigned calls;
    static const struct {
        const char *name;
        uint32_t size;
        uint32_t peak;
    } pools[BENCH_REPORT_POOLS_MAX] = {
        {"rx_pkt", 192u, 20u}, {"tx_pkt", 32u, 6u}, {"rx_buf", 1024u, 150u}, {"tx_buf", 768u, 90u}};

    calls++;
    if (calls == 3u) {
        return false;
    }
    for (uint32_t i = 0; i < BENCH_REPORT_POOLS_MAX; i++) {
        out->pools[i] = (bench_report_pool_t){.name = pools[i].name,
                                              .size = pools[i].size,
                                              .used = 3u,
                                              .peak = pools[i].peak + calls};
    }
    out->pool_count = BENCH_REPORT_POOLS_MAX;
    out->tcp_rexmit = calls - 1u;
    return true;
}

static void note_worker(void)
{
    /* What the provider keeps for the workers that ended, here a made-up figure. */
    (void)bench_report_core_note_ended("ssh_worker", 16384u, 6000u, false);
}

static void sleeper(void *first, void *second, void *third)
{
    ARG_UNUSED(first);
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    for (;;) {
        k_sleep(K_FOREVER);
    }
}

int main(void)
{
    static const bench_report_core_config_t config = {
        .activity = read_activity,
        .refresh = note_worker,
        .arena_bytes = CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE,
        .net = fake_net,
    };

    (void)k_thread_create(&http_thread, http_stack, K_THREAD_STACK_SIZEOF(http_stack), sleeper,
                          NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    (void)k_thread_name_set(&http_thread, "http_server_tid");
    /* No name: the report must print thread@<address>. */
    (void)k_thread_create(&anonymous_thread, anonymous_stack,
                          K_THREAD_STACK_SIZEOF(anonymous_stack), sleeper, NULL, NULL, NULL, 5, 0,
                          K_NO_WAIT);
    if (!bench_report_core_start(&config) || bench_report_core_start(&config)) {
        printk("BENCH_REPORT_TEST start did not behave: once, and not twice\n");
        return 1;
    }
    /* The main thread's figure is noted for the ended row. */
    {
        size_t unused = 0;
        k_tid_t self = k_current_get();

        if (k_thread_stack_space_get(self, &unused) == 0) {
            (void)bench_report_core_note_ended("main", (uint32_t)self->stack_info.size,
                                               (uint32_t)unused, true);
        }
    }
    k_msleep(10000);
    for (int i = 0; i < 5; i++) {
        activity += 3u;
        k_msleep(100);
    }
    k_msleep(46000 - 10500);
    for (uint32_t i = 0; i < EXTRA_THREADS; i++) {
        (void)k_thread_create(&extra_threads[i], extra_stacks[i], 512, sleeper, NULL, NULL, NULL, 5,
                              0, K_NO_WAIT);
    }
    k_msleep(100);
    /* Not through the policy: the policy would wait for the minimum gap, and this one is a bound.
     */
    bench_report_core_snapshot(BENCH_REPORT_IDLE);
    k_msleep(1000);
    printk("BENCH_REPORT_TEST complete\n");
    return 0;
}
