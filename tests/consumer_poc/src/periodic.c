/* Policy follows samples/messaging/controller.c: skip whole periods, keep the phase. */
#include "periodic.h"

#include <stddef.h>

int periodic_start(periodic_t *state, const periodic_clock_t *clock, int64_t period_ns)
{
    int64_t now = 0;
    if (state == NULL || clock == NULL || clock->now_ns == NULL || period_ns <= 0)
        return 1;
    if (clock->now_ns(clock->context, &now) != 0 || now > INT64_MAX - period_ns)
        return 1;
    state->period_ns = period_ns;
    state->deadline_ns = now + period_ns;
    return 0;
}

int periodic_next(periodic_t *state, const periodic_clock_t *clock, uint64_t *skipped)
{
    int64_t now = 0;
    if (state == NULL || clock == NULL || clock->now_ns == NULL || skipped == NULL)
        return 1;
    if (clock->now_ns(clock->context, &now) != 0 || now < state->deadline_ns ||
        now > INT64_MAX - state->period_ns)
        return 1;
#ifdef CONSUMER_DEFECT_REBASE
    /* Controlled defect for the functional oracle: rebases the phase, reports no skip. */
    state->deadline_ns = now + state->period_ns;
    *skipped = 0;
#else
    int64_t steps = (now - state->deadline_ns) / state->period_ns + 1;
    state->deadline_ns += steps * state->period_ns;
    *skipped = (uint64_t)(steps - 1);
#endif
    return 0;
}
