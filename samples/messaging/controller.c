/* Separate portable reference-controller timing scenario. Historical controller
 * fixtures are unchanged. Phase and skip policy belong to this consumer. */
#include <crux/thread.h>
#include <crux/time.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static crux_status_t advance(int64_t *deadline, int64_t now, uint64_t *skipped)
{
    const int64_t period_ms = 100;
    if (deadline == NULL || skipped == NULL || *deadline < 0 || now < *deadline)
        return CRUX_ERR_INVALID_ARGUMENT;
    if (now > INT64_MAX - period_ms)
        return CRUX_ERR_OUT_OF_RANGE;
    int64_t steps = (now - *deadline) / period_ms + 1;
    *deadline += steps * period_ms;
    *skipped = (uint64_t)(steps - 1);
    return CRUX_OK;
}

int main(void)
{
    int64_t deadline = 1100;
    uint64_t skipped = 0;
    /* On the 100 ms grid, 1350 skips 1200 and 1300 after servicing 1100. */
    if (advance(&deadline, 1350, &skipped) != CRUX_OK || deadline != 1400 || skipped != 2)
        return 1;
    crux_monotonic_time_t release, now;
    if (crux_monotonic_deadline_after(crux_duration_from_ms(100), &release) != CRUX_OK)
        return 2;
    if (crux_thread_sleep_until(release) != CRUX_OK || crux_monotonic_now(&now) != CRUX_OK ||
        crux_monotonic_is_before(now, release))
        return 3;
    /* Keep the original typed phase; skip whole periods without rebasing it. */
    const int64_t period_ns = INT64_C(100000000);
    int64_t elapsed = crux_duration_to_ns(crux_monotonic_diff(now, release));
    if (elapsed > INT64_MAX - period_ns)
        return 4;
    int64_t steps = elapsed / period_ns + 1;
    if (crux_monotonic_add(release, crux_duration_from_ns(steps * period_ns), &release) !=
            CRUX_OK ||
        !crux_monotonic_is_before(now, release))
        return 4;
    puts("MESSAGING_CONTROLLER passed absolute release and consumer-owned skip policy");
    return 0;
}
