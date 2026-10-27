#include "controller.h"

#include <crux/time_authority.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static int64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

int main(void)
{
    crux_status_t status = crux_realtime_init();
    printf("controller composition=host-controlled init=%d\n", status);
    if (status != CRUX_OK) {
        return 1;
    }
    controller_schedule_t schedule = {.deadline_ms = monotonic_ms(), .period_ms = 100};
    if (schedule.deadline_ms < 0) {
        return 1;
    }
    for (uint32_t cycle = 0; cycle < 20; ++cycle) {
        struct timespec deadline = {.tv_sec = schedule.deadline_ms / 1000,
                                    .tv_nsec = (schedule.deadline_ms % 1000) * 1000000};
        int error;
        do {
            error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
        } while (error == EINTR);
        if (error != 0) {
            return 1;
        }
        int64_t released = monotonic_ms();
        if (released < schedule.deadline_ms) {
            return 1;
        }
        if (cycle == 2 || cycle == 6) {
            const crux_realtime_t value = {cycle == 2 ? INT64_C(1792238400) : INT64_C(1792234800),
                                           0};
            status = crux_realtime_set(&value, CRUX_REALTIME_SOURCE_MANUAL, false);
            if (status != CRUX_OK) {
                printf("controller set_error=%d\n", status);
                return 1;
            }
        }
        controller_reading_t reading = controller_observe(cycle);
        printf("cycle=%u status=%d valid=%d ", reading.cycle, reading.status,
               reading.status == CRUX_OK);
        if (reading.status == CRUX_OK) {
            printf("utc=%lld ", (long long)reading.utc_seconds);
        } else {
            printf("utc=unavailable ");
        }
        printf("monotonic_ms=%lld lateness_ms=%lld\n", (long long)released,
               (long long)(released - schedule.deadline_ms));
        int64_t finished = monotonic_ms();
        if (controller_schedule_advance(&schedule, finished) != CRUX_OK) {
            return 1;
        }
        printf("schedule skipped=%llu\n", (unsigned long long)schedule.skipped);
    }
    puts("CONTROLLER_COMPLETE cycles=20");
    return 0;
}
