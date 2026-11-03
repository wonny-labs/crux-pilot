/* Binds the application's clock port to public Crux capabilities and runs a finite
 * periodic scenario with one deliberate overrun. */
#include "app_run.h"

#include "periodic.h"

#include <crux/thread.h>
#include <crux/time.h>

#include <stddef.h>

#define PERIOD_MS 20

static int crux_clock_now(void *context, int64_t *out)
{
    (void)context;
    crux_monotonic_time_t now;
    if (crux_monotonic_now(&now) != CRUX_OK)
        return 1;
    *out = crux_duration_to_ns(crux_monotonic_diff(now, (crux_monotonic_time_t){0}));
    return 0;
}

static crux_status_t sleep_until_ns(int64_t deadline_ns)
{
    crux_monotonic_time_t deadline;
    crux_status_t status = crux_monotonic_add((crux_monotonic_time_t){0},
                                              crux_duration_from_ns(deadline_ns), &deadline);
    return status == CRUX_OK ? crux_thread_sleep_until(deadline) : status;
}

int app_run(app_report_t report, void *context)
{
    const periodic_clock_t clock = {crux_clock_now, NULL};
    const int64_t period_ns = crux_duration_to_ns(crux_duration_from_ms(PERIOD_MS));
    periodic_t state;
    if (report == NULL || periodic_start(&state, &clock, period_ns) != 0)
        return 1;
    for (int cycle = 1; cycle <= APP_CYCLES; cycle++) {
        if (sleep_until_ns(state.deadline_ns) != CRUX_OK)
            return 2;
        if (cycle == APP_OVERRUN_CYCLE &&
            sleep_until_ns(state.deadline_ns + period_ns * 5 / 2) != CRUX_OK)
            return 3;
        uint64_t skipped = 0;
        if (periodic_next(&state, &clock, &skipped) != 0)
            return 4;
        report(context, cycle, skipped);
    }
    return 0;
}
