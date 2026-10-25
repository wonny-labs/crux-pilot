#ifndef SYSTEM_CONSUMER_RUNTIME_H
#define SYSTEM_CONSUMER_RUNTIME_H

#include <crux/system.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Caller-owned observation, not a health/readiness verdict. cause is valid only
 * when cause_status is OK. Retain/distribute this report before the designated
 * diagnostic owner explicitly calls crux_system_reset_cause_clear(). */
typedef struct {
    crux_system_status_t services;
    crux_status_t cause_status;
    crux_reset_cause_t cause;
} system_runtime_observation_t;

/* Non-overlapping task-context calls. OK means an observation was produced;
 * individual service/cause errors remain in the report. A failed status query
 * leaves out unchanged; a failed cause query publishes its status and zero cause.
 * No start/feed/clear/reset occurs. Each explicit cause read may permit a later
 * public clear, but this helper never elects to clear or claims delivery. */
crux_status_t system_runtime_observe(system_runtime_observation_t *out);
crux_status_t system_cpp_observe(system_runtime_observation_t *out);

/* Caller owns the token/lifecycle and defines required work/freshness. Failed
 * work is propagated without a feed. OK work permits one feed and returns its
 * result. Status-query success is not evidence of completed work. Call start,
 * clear and reset explicitly through the public Crux API when policy permits. */
crux_status_t system_runtime_feed_after_work(crux_watchdog_t watchdog, crux_status_t work_status);
crux_status_t system_cpp_feed_after_work(crux_watchdog_t watchdog, crux_status_t work_status);

#ifdef __cplusplus
}
#endif
#endif
