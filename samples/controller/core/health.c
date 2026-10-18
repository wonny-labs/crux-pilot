#include "controller_health.h"

#include <stddef.h>

bool controller_health_init(controller_health_t *health, int64_t freshness_ms) {
    if (health == NULL || freshness_ms <= 0) {
        return false;
    }
    *health = (controller_health_t){
        .last_observation_ms = -1, .last_progress_ms = -1, .freshness_ms = freshness_ms};
    return true;
}

bool controller_health_observe(controller_health_t *health, int64_t now_ms, int64_t completed_ms,
                               uint32_t sequence, bool work_ok) {
    if (health == NULL || health->freshness_ms <= 0 || now_ms < 0 ||
        now_ms < health->last_observation_ms) {
        return false;
    }
    health->last_observation_ms = now_ms;
    if (!work_ok || completed_ms < 0 || completed_ms > now_ms ||
        completed_ms < health->last_progress_ms) {
        health->last_sequence = sequence;
        health->progress_seen = false;
        return false;
    }
    if (sequence != health->last_sequence) {
        health->last_sequence = sequence;
        health->last_progress_ms = completed_ms;
        health->progress_seen = true;
    }
    return health->progress_seen && now_ms - health->last_progress_ms <= health->freshness_ms;
}

controller_readiness_t controller_readiness(bool time_ready, bool device_ready, bool network_ready,
                                            bool optional_ready) {
    if (!time_ready || !device_ready || !network_ready) {
        return CONTROLLER_NOT_READY;
    }
    return optional_ready ? CONTROLLER_READY : CONTROLLER_DEGRADED;
}
