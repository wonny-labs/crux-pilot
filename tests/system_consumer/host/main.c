#include "controller_system.h"
#include "runtime.h"
#include <assert.h>
int main(void) {
    system_runtime_observation_t c, cpp;
    assert(system_runtime_observe(&c) == CRUX_OK);
    assert(system_cpp_observe(&cpp) == CRUX_OK);
    assert(c.services.watchdog_emulated && cpp.services.watchdog_emulated);
    assert(c.cause_status == CRUX_ERR_NOT_SUPPORTED && cpp.cause_status == CRUX_ERR_NOT_SUPPORTED);
    assert(c.services.reset_status == CRUX_ERR_NOT_SUPPORTED);
    assert(crux_system_reset() == CRUX_ERR_NOT_SUPPORTED);
    controller_system_t controller;
    assert(controller_system_boot(&controller) == CRUX_OK);
    assert(controller.initial_cause_status == CRUX_ERR_NOT_SUPPORTED);
    assert(controller.time_health.freshness_ms == 500 &&
           controller.network_health.freshness_ms == 16000);
    crux_watchdog_t token = {0};
    assert(crux_watchdog_start(2000, &token) == CRUX_OK);
    assert(system_runtime_feed_after_work(token, CRUX_ERR_IO) == CRUX_ERR_IO);
    assert(system_cpp_feed_after_work(token, CRUX_OK) == CRUX_OK);
    controller_progress_t progress = {.sequence = 1, .completed_ms = 0, .ok = true};
    assert(controller_system_feed(&controller, token, 0, progress, progress) == CRUX_OK);
    assert(controller_system_feed(&controller, token, 501, progress, progress) ==
           CRUX_ERR_NOT_READY);
    progress.sequence = 2;
    progress.completed_ms = 16000;
    controller_progress_t old_network = {.sequence = 1, .completed_ms = 0, .ok = true};
    assert(controller_system_feed(&controller, token, 16000, progress, old_network) == CRUX_OK);
    progress.sequence = 3;
    progress.completed_ms = 16001;
    assert(controller_system_feed(&controller, token, 16001, progress, old_network) ==
           CRUX_ERR_NOT_READY);
    assert(controller.initial_cause_status == CRUX_ERR_NOT_SUPPORTED);
    return 0;
}
