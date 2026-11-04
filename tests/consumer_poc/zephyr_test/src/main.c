/* Zephyr native_sim level: simulated time makes the skip count exact. */
#include "app_run.h"

#include <zephyr/ztest.h>

static uint64_t skips[APP_CYCLES + 1];
static int last_cycle;

static void record(void *context, int cycle, uint64_t skipped)
{
    (void)context;
    zassert_true(cycle >= 1 && cycle <= APP_CYCLES);
    skips[cycle] = skipped;
    last_cycle = cycle;
}

ZTEST(consumer_periodic, test_overrun_skips_whole_periods_on_simulated_time)
{
    zassert_equal(app_run(record, NULL), 0);
    zassert_equal(last_cycle, APP_CYCLES);
    for (int cycle = 1; cycle <= APP_CYCLES; cycle++) {
        uint64_t expected = cycle == APP_OVERRUN_CYCLE ? APP_OVERRUN_SKIPS : 0;
        zassert_equal(skips[cycle], expected, "cycle %d", cycle);
    }
}

ZTEST_SUITE(consumer_periodic, NULL, NULL, NULL, NULL, NULL);
