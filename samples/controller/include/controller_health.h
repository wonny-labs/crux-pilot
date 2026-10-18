#ifndef CONTROLLER_HEALTH_H
#define CONTROLLER_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

/* Sample policy only. Composition supplies a consistent completed-work snapshot.
 * Observing a changed counter must not make an old completion fresh. */
typedef struct {
    int64_t last_observation_ms;
    int64_t last_progress_ms;
    int64_t freshness_ms;
    uint32_t last_sequence;
    bool progress_seen;
} controller_health_t;

typedef enum { CONTROLLER_NOT_READY, CONTROLLER_READY, CONTROLLER_DEGRADED } controller_readiness_t;

#ifdef __cplusplus
extern "C" {
#endif
bool controller_health_init(controller_health_t *health, int64_t freshness_ms);
bool controller_health_observe(controller_health_t *health, int64_t now_ms, int64_t completed_ms,
                               uint32_t sequence, bool work_ok);
controller_readiness_t controller_readiness(bool time_ready, bool device_ready, bool network_ready,
                                            bool optional_ready);
#ifdef __cplusplus
}
#endif
#endif
