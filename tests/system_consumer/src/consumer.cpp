#include "runtime.h"

extern "C" crux_status_t system_cpp_observe(system_runtime_observation_t *out) {
    system_runtime_observation_t observation{};

    if (out == nullptr)
        return CRUX_ERR_INVALID_ARGUMENT;
    const crux_status_t status = crux_system_status_get(&observation.services);
    if (status != CRUX_OK)
        return status;
    observation.cause_status = crux_system_reset_cause_get(&observation.cause);
    *out = observation;
    return CRUX_OK;
}

extern "C" crux_status_t system_cpp_feed_after_work(crux_watchdog_t watchdog,
                                                    crux_status_t work_status) {
    if (work_status != CRUX_OK)
        return work_status;
    return crux_watchdog_feed(watchdog);
}
