#include "controller.h"

#include <crux/time.h>
#include <limits.h>
#include <stddef.h>

controller_reading_t controller_observe(uint32_t cycle) {
    controller_reading_t reading = {.cycle = cycle, .utc_seconds = 0};
    crux_time_instant_t value;
    reading.status = crux_time_get(&value);
    if (reading.status == CRUX_OK) {
        reading.utc_seconds = value;
    }
    return reading;
}

crux_status_t controller_schedule_advance(controller_schedule_t *schedule, int64_t now_ms) {
    if (schedule == NULL || schedule->deadline_ms < 0 || now_ms < schedule->deadline_ms ||
        schedule->period_ms <= 0) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (now_ms > INT64_MAX - schedule->period_ms) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    int64_t steps = (now_ms - schedule->deadline_ms) / schedule->period_ms + 1;
    schedule->deadline_ms += steps * schedule->period_ms;
    schedule->skipped = (uint64_t)(steps - 1);
    return CRUX_OK;
}
