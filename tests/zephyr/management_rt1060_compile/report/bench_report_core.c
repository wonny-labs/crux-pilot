#include "bench_report_core.h"

#include "bench_report_policy.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/sys_heap.h>

#if !defined(CONFIG_THREAD_MONITOR) || !defined(CONFIG_THREAD_STACK_INFO) ||                       \
    !defined(CONFIG_INIT_STACKS) || !defined(CONFIG_THREAD_NAME)
#error "The bench report reads stack use: it needs THREAD_MONITOR, THREAD_STACK_INFO, INIT_STACKS and THREAD_NAME"
#endif
#if !defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
#error "The bench report reads the heap's peak: it needs SYS_HEAP_RUNTIME_STATS"
#endif

/* In Zephyr's malloc.c, with no public header at this revision. */
int malloc_runtime_stats_get(struct sys_memory_stats *stats);

typedef struct {
    char name[BENCH_REPORT_NAME_MAX];
    uint32_t size;
    uint32_t unused;
    bool ended;
    bool supersedes_live;
} row_t;

static row_t rows[BENCH_REPORT_THREADS_MAX];
static uint32_t row_count;
static uint32_t rows_missed;
static row_t ended_rows[BENCH_REPORT_ENDED_MAX];
static uint32_t ended_count;
static char line[BENCH_REPORT_LINE_MAX];
static uint32_t sequence;
static uint32_t bursts_seen;
static bench_report_core_config_t config;

static K_THREAD_STACK_DEFINE(report_stack, BENCH_REPORT_STACK_BYTES);
static struct k_thread report_thread;
static atomic_t started;
static K_MUTEX_DEFINE(report_lock);

bool bench_report_core_note_ended(const char *name, uint32_t size, uint32_t unused,
                                  bool supersedes_live)
{
    char clean[BENCH_REPORT_NAME_MAX];
    bool stored = false;

    if (bench_report_sanitize_name(clean, sizeof(clean), name, 0u) == 0u || unused > size) {
        return false;
    }
    (void)k_mutex_lock(&report_lock, K_FOREVER);
    for (uint32_t i = 0; i < ended_count; i++) {
        if (strcmp(ended_rows[i].name, clean) == 0) {
            if (unused < ended_rows[i].unused) {
                ended_rows[i].unused = unused;
            }
            ended_rows[i].size = size;
            stored = true;
            break;
        }
    }
    if (!stored && ended_count < BENCH_REPORT_ENDED_MAX) {
        row_t *row = &ended_rows[ended_count++];
        memcpy(row->name, clean, sizeof(row->name));
        row->size = size;
        row->unused = unused;
        row->ended = true;
        row->supersedes_live = supersedes_live;
        stored = true;
    }
    k_mutex_unlock(&report_lock);
    return stored;
}

static bool superseded(const char *name)
{
    for (uint32_t i = 0; i < ended_count; i++) {
        if (ended_rows[i].supersedes_live && strcmp(ended_rows[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Called for each thread, without the kernel's lock held: a row is copied, nothing is printed. */
static void collect(const struct k_thread *thread, void *unused_argument)
{
    size_t unused = 0;
    row_t *row;
    const char *name = k_thread_name_get((k_tid_t)thread);

    (void)unused_argument;
    if (k_thread_stack_space_get(thread, &unused) != 0) {
        return;
    }
    if (row_count >= BENCH_REPORT_THREADS_MAX) {
        rows_missed++;
        return;
    }
    row = &rows[row_count];
    (void)bench_report_sanitize_name(row->name, sizeof(row->name), name, (uintptr_t)thread);
    if (superseded(row->name)) {
        return;
    }
    row->size = (uint32_t)thread->stack_info.size;
    row->unused = (uint32_t)MIN(unused, thread->stack_info.size);
    row->ended = false;
    row_count++;
}

static void emit(void)
{
    printk("%s\n", line);
    k_msleep(BENCH_REPORT_LINE_PACE_MS);
}

void bench_report_core_snapshot(bench_report_reason_t reason)
{
    struct sys_memory_stats heap = {0};
    uint32_t printed = 0;

    (void)k_mutex_lock(&report_lock, K_FOREVER);
    if (config.refresh != NULL) {
        config.refresh();
    }
    row_count = 0u;
    rows_missed = 0u;
    k_thread_foreach_unlocked(collect, NULL);
    (void)malloc_runtime_stats_get(&heap);
    sequence++;

    if (bench_report_format_begin(line, sizeof(line), sequence, reason, k_uptime_get_32(),
                                  bursts_seen) != 0u) {
        emit();
    }
    for (uint32_t i = 0; i < row_count; i++) {
        if (bench_report_format_stack(line, sizeof(line), sequence, rows[i].name, rows[i].size,
                                      rows[i].unused, false) != 0u) {
            emit();
            printed++;
        } else {
            rows_missed++;
        }
    }
    for (uint32_t i = 0; i < ended_count; i++) {
        if (bench_report_format_stack(line, sizeof(line), sequence, ended_rows[i].name,
                                      ended_rows[i].size, ended_rows[i].unused, true) != 0u) {
            emit();
            printed++;
        } else {
            rows_missed++;
        }
    }
    if (bench_report_format_heap(line, sizeof(line), sequence, config.arena_bytes,
                                 (uint32_t)heap.allocated_bytes,
                                 (uint32_t)heap.max_allocated_bytes,
                                 (uint32_t)heap.free_bytes) != 0u) {
        emit();
        printed++;
    }
    if (bench_report_format_end(line, sizeof(line), sequence, printed, rows_missed) != 0u) {
        emit();
    }
    k_mutex_unlock(&report_lock);
}

static void report_loop(void *first, void *second, void *third)
{
    bench_report_policy_t policy;

    ARG_UNUSED(first);
    ARG_UNUSED(second);
    ARG_UNUSED(third);
    bench_report_policy_init(&policy, k_uptime_get_32(), config.activity());
    for (;;) {
        bench_report_action_t action =
            bench_report_policy_step(&policy, k_uptime_get_32(), config.activity());

        bursts_seen = policy.bursts;
        if (action == BENCH_REPORT_ACTION_BOOT) {
            bench_report_core_snapshot(BENCH_REPORT_BOOT);
        } else if (action == BENCH_REPORT_ACTION_QUIET) {
            bench_report_core_snapshot(BENCH_REPORT_QUIET);
        } else if (action == BENCH_REPORT_ACTION_IDLE) {
            bench_report_core_snapshot(BENCH_REPORT_IDLE);
        }
        k_msleep(BENCH_REPORT_POLL_MS);
    }
}

bool bench_report_core_start(const bench_report_core_config_t *settings)
{
    if (settings == NULL || settings->activity == NULL || !atomic_cas(&started, 0, 1)) {
        return false;
    }
    config = *settings;
    (void)k_thread_create(&report_thread, report_stack, K_THREAD_STACK_SIZEOF(report_stack),
                          report_loop, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0,
                          K_NO_WAIT);
    (void)k_thread_name_set(&report_thread, "bench_report");
    return true;
}
