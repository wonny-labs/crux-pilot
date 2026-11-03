/* Host integration level: the same scenario on the real Linux clock and sleep, and
 * the documented controlled Time provider. Scheduling is real, so skips are a lower bound. */
#include "app_run.h"

#include <crux/time.h>

#include <stdio.h>

typedef struct {
    int cycles;
    uint64_t skipped;
    uint64_t overrun_skipped;
} record_t;

static void record(void *context, int cycle, uint64_t skipped)
{
    record_t *totals = context;
    totals->cycles = cycle;
    totals->skipped += skipped;
    if (cycle == APP_OVERRUN_CYCLE)
        totals->overrun_skipped = skipped;
}

int main(void)
{
    crux_realtime_t unset;
    if (crux_realtime_init() != CRUX_OK || crux_realtime_now(&unset) != CRUX_ERR_NO_DATA) {
        puts("FAILED the memory provider starts ready but unset");
        return 1;
    }
    record_t totals = {0, 0, 0};
    int result = app_run(record, &totals);
    printf("CONSUMER_INTEGRATION result=%d cycles=%d skipped=%llu\n", result, totals.cycles,
           (unsigned long long)totals.skipped);
    return result != 0 || totals.cycles != APP_CYCLES || totals.overrun_skipped < APP_OVERRUN_SKIPS;
}
