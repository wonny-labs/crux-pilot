#include "controller_health.h"

#include <stddef.h>

bool controller_health_init(controller_health_t *health, int64_t freshness_ms) {
    if (health == NULL || freshness_ms <= 0) {
        return false;
    }
    *health = (controller_health_t){.last_observation_ms = -1,
                                    .last_progress_ms = -1,
                                    .freshness_ms = freshness_ms,
                                    .recovery_after_ms = -1};
    return true;
}

static bool reject_observation(controller_health_t *health, uint32_t sequence) {
    health->last_sequence = sequence;
    health->progress_seen = false;
    health->recovery_after_ms = health->last_observation_ms;
    return false;
}

bool controller_health_observe(controller_health_t *health, int64_t now_ms,
                               controller_progress_t progress) {
    if (health == NULL || health->freshness_ms <= 0) {
        return false;
    }
    if (now_ms < 0 || now_ms < health->last_observation_ms) {
        return reject_observation(health, progress.sequence);
    }
    health->last_observation_ms = now_ms;
    if (!progress.ok || progress.completed_ms < 0 || progress.completed_ms > now_ms ||
        progress.completed_ms < health->last_progress_ms ||
        progress.completed_ms <= health->recovery_after_ms) {
        return reject_observation(health, progress.sequence);
    }
    if (progress.sequence != health->last_sequence) {
        if (progress.completed_ms <= health->last_progress_ms) {
            return reject_observation(health, progress.sequence);
        }
        health->last_sequence = progress.sequence;
        health->last_progress_ms = progress.completed_ms;
        health->progress_seen = true;
    }
    return health->progress_seen && now_ms - health->last_progress_ms <= health->freshness_ms;
}

bool controller_health_joint_observe(controller_health_t *time_health,
                                     controller_health_t *network_health, int64_t now_ms,
                                     controller_progress_t time_progress,
                                     controller_progress_t network_progress) {
    bool time_ok = controller_health_observe(time_health, now_ms, time_progress);
    bool network_ok = controller_health_observe(network_health, now_ms, network_progress);
    return time_ok && network_ok;
}

bool controller_m3_arm_allowed(int64_t now_ms, int64_t healthy_since_ms, int64_t window_end_ms) {
    return now_ms >= 0 && healthy_since_ms >= 0 && now_ms >= healthy_since_ms &&
           window_end_ms >= now_ms && now_ms - healthy_since_ms >= 1000 &&
           window_end_ms - now_ms >= 40000;
}

bool controller_m3_fault_allowed(int64_t now_ms, int64_t request_deadline_ms,
                                 int64_t window_end_ms) {
    return now_ms >= 0 && request_deadline_ms > now_ms && window_end_ms >= now_ms &&
           window_end_ms - now_ms >= 25000;
}

controller_readiness_t controller_readiness(bool time_ready, bool device_ready, bool network_ready,
                                            bool optional_ready) {
    if (!time_ready || !device_ready || !network_ready) {
        return CONTROLLER_NOT_READY;
    }
    return optional_ready ? CONTROLLER_READY : CONTROLLER_DEGRADED;
}
