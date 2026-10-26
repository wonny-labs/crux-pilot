#include "controller_system.h"
#include <stddef.h>
crux_status_t controller_system_boot(controller_system_t *out)
{
    if (out == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    controller_system_t value = {0};
    crux_status_t result = crux_system_status_get(&value.initial_services);
    if (result != CRUX_OK)
        return result;
    value.initial_cause_status = crux_system_reset_cause_get(&value.initial_cause);
    (void)controller_health_init(&value.time_health, 500);
    (void)controller_health_init(&value.network_health, 16000);
    *out = value;
    return CRUX_OK;
}
crux_status_t controller_system_feed(controller_system_t *state, crux_watchdog_t watchdog,
                                     int64_t now_ms, controller_progress_t time,
                                     controller_progress_t network)
{
    if (state == NULL)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (!controller_health_joint_observe(&state->time_health, &state->network_health, now_ms, time,
                                         network))
        return CRUX_ERR_NOT_READY;
    return crux_watchdog_feed(watchdog);
}
