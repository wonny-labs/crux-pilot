#include "bench_report_policy.h"

void bench_report_policy_init(bench_report_policy_t *policy, uint32_t now_ms, uint32_t segments)
{
    *policy = (bench_report_policy_t){
        .last_segments = segments,
        .last_change_ms = now_ms,
        .last_print_ms = now_ms,
    };
}

bench_report_action_t bench_report_policy_step(bench_report_policy_t *policy, uint32_t now_ms,
                                               uint32_t segments)
{
    if (!policy->booted) {
        policy->booted = true;
        policy->last_print_ms = now_ms;
        policy->last_change_ms = now_ms;
        policy->last_segments = segments;
        return BENCH_REPORT_ACTION_BOOT;
    }
    if (segments != policy->last_segments) {
        policy->last_segments = segments;
        policy->last_change_ms = now_ms;
        policy->idle_done = false;
        if (!policy->active) {
            policy->active = true;
            policy->bursts++;
        }
        return BENCH_REPORT_ACTION_NONE;
    }
    uint32_t still = now_ms - policy->last_change_ms;
    uint32_t since_print = now_ms - policy->last_print_ms;
    if (since_print < BENCH_REPORT_MIN_GAP_MS) {
        return BENCH_REPORT_ACTION_NONE;
    }
    if (policy->active && still >= BENCH_REPORT_QUIET_MS) {
        policy->active = false;
        policy->last_print_ms = now_ms;
        return BENCH_REPORT_ACTION_QUIET;
    }
    if (!policy->active && !policy->idle_done && still >= BENCH_REPORT_IDLE_MS) {
        policy->idle_done = true;
        policy->last_print_ms = now_ms;
        return BENCH_REPORT_ACTION_IDLE;
    }
    return BENCH_REPORT_ACTION_NONE;
}
