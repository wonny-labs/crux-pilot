/*
 * When the bench report prints (row C2). Pure C: the thread feeds it the time and a counter of the
 * TCP segments that crossed the interface, and it answers with what to print now. No Zephyr, so a
 * host test runs it over traces, including the wrap of both 32-bit values.
 *
 * Three prints, all bounded: BOOT once at the start; QUIET once a burst of segments has stopped for
 * BENCH_REPORT_QUIET_MS; IDLE once when nothing has moved for BENCH_REPORT_IDLE_MS after that. No
 * two prints are closer than BENCH_REPORT_MIN_GAP_MS, so the output rate has a ceiling whatever
 * arrives on the wire.
 */
#ifndef CRUX_BENCH_REPORT_POLICY_H
#define CRUX_BENCH_REPORT_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define BENCH_REPORT_POLL_MS 250u
#define BENCH_REPORT_QUIET_MS 2000u
#define BENCH_REPORT_MIN_GAP_MS 5000u
#define BENCH_REPORT_IDLE_MS 30000u

typedef enum {
    BENCH_REPORT_ACTION_NONE = 0,
    BENCH_REPORT_ACTION_BOOT,
    BENCH_REPORT_ACTION_QUIET,
    BENCH_REPORT_ACTION_IDLE,
} bench_report_action_t;

typedef struct {
    uint32_t last_segments;
    uint32_t last_change_ms;
    uint32_t last_print_ms;
    uint32_t bursts;
    bool booted;
    bool active;
    bool idle_done;
} bench_report_policy_t;

void bench_report_policy_init(bench_report_policy_t *policy, uint32_t now_ms, uint32_t segments);
/** Call every BENCH_REPORT_POLL_MS or so. Differences are taken modulo 2^32. */
bench_report_action_t bench_report_policy_step(bench_report_policy_t *policy, uint32_t now_ms,
                                               uint32_t segments);

#endif
