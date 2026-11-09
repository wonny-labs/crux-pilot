/*
 * The bench report's kernel side (row C2 of docs/rt1060-management-image-feasibility.md): a thread
 * that watches an activity counter, and prints the stack watermark of every thread and the heap's
 * in-use and peak on the UART, in the lines of bench_report_format.h. Generic Zephyr: no board and
 * no bench symbol, so tests/native/bench_report builds it on native_sim. The bench image adds its
 * own activity counter and its extra rows in ../src/bench_report.c.
 *
 * Bounded by construction: a table of BENCH_REPORT_THREADS_MAX rows and one line buffer, both
 * static; no allocation; one line is printed, then the thread sleeps BENCH_REPORT_LINE_PACE_MS so
 * that the log buffer that printk goes through never holds more than a line or two; prints are at
 * most one per BENCH_REPORT_MIN_GAP_MS (bench_report_policy.h).
 */
#ifndef CRUX_BENCH_REPORT_CORE_H
#define CRUX_BENCH_REPORT_CORE_H

#include "bench_report_format.h"

#include <stdbool.h>
#include <stdint.h>

/** Rows of threads in one report. More threads than this are counted in `missed=` of the end line.
 */
#define BENCH_REPORT_THREADS_MAX 48u
/** Rows of threads that have ended, which no thread list shows any more. */
#define BENCH_REPORT_ENDED_MAX 4u
/** Pause after each printed line, in milliseconds: 160 characters take 14 ms at 115,200 baud. */
#define BENCH_REPORT_LINE_PACE_MS 15u
/** Stack of the reporting thread. Its own row in every report shows how much of it is used. */
#define BENCH_REPORT_STACK_BYTES 2048u

/** Pools in one report of row C6: the packets and the data buffers, each way. */
#define BENCH_REPORT_POOLS_MAX 4u

typedef struct {
    const char *name; /**< "rx_pkt", "tx_pkt", "rx_buf" or "tx_buf": lower case letters and '_' */
    uint32_t size;    /**< The count the pool was built with */
    uint32_t used;    /**< In use now */
    uint32_t peak;    /**< The most that was ever in use */
} bench_report_pool_t;

/** What the image's network stack says, read once per report. */
typedef struct {
    bench_report_pool_t pools[BENCH_REPORT_POOLS_MAX];
    uint32_t pool_count;
    uint32_t ip_drop;
    uint32_t tcp_drop;
    uint32_t tcp_seg_drop;
    uint32_t tcp_rexmit;
    uint32_t tcp_conndrop;
} bench_report_net_t;

typedef struct {
    /** A counter that moves when a client is talking, such as TCP segments. Required. */
    uint32_t (*activity)(void);
    /** Called before each snapshot, for the caller to note threads that have ended. May be NULL. */
    void (*refresh)(void);
    /** The heap's configured size, printed as `arena=`. */
    uint32_t arena_bytes;
    /**
     * Fills the network pools and counters for the report (row C6): called once per snapshot, after
     * the heap line is read. Returns false when it cannot, and then no BENCH-POOL or BENCH-NET line
     * is printed. May be NULL, as in the native_sim image, which has no network.
     */
    bool (*net)(bench_report_net_t *out);
} bench_report_core_config_t;

/** Starts the reporting thread, once. Returns false for a second call or a NULL activity. */
bool bench_report_core_start(const bench_report_core_config_t *config);

/**
 * Records a thread that is gone, with its stack size and the bytes it never touched, so that every
 * later report carries a `state=ended` row for it. A second call with the same name keeps the lower
 * unused figure. `supersedes_live` hides a live row of that name, for a thread such as main, whose
 * figure at exit is the final one. Returns false when the table of ENDED_MAX rows is full.
 */
bool bench_report_core_note_ended(const char *name, uint32_t size, uint32_t unused,
                                  bool supersedes_live);

/** Prints one report now, from the calling thread. The reporting thread calls it, and a test may.
 */
void bench_report_core_snapshot(bench_report_reason_t reason);

#endif
