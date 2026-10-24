#ifndef CONTROLLER_SYSTEM_H
#define CONTROLLER_SYSTEM_H
#include "controller_health.h"
#include <crux/system.h>
/* Sample-owned boot diagnostics and work policy; no platform health registry.
 * Designated composition owner retains/distributes this snapshot before choosing
 * an explicit clear. This example does not clear, reset or auto-arm. */
typedef struct {
    crux_system_status_t initial_services;
    crux_status_t initial_cause_status;
    crux_reset_cause_t initial_cause;
    controller_health_t time_health;
    controller_health_t network_health;
} controller_system_t;
crux_status_t controller_system_boot(controller_system_t *out);
crux_status_t controller_system_feed(controller_system_t *state, crux_watchdog_t watchdog,
                                     int64_t now_ms, controller_progress_t time,
                                     controller_progress_t network);
#endif
