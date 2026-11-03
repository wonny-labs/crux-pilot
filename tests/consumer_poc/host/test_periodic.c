/* Unit level: the policy against the consumer's own scripted clock. No Crux target. */
#include "periodic.h"

#include <stdio.h>

typedef struct {
    int64_t value_ns;
    int result;
} scripted_clock_t;

static int scripted_now(void *context, int64_t *out)
{
    const scripted_clock_t *script = context;
    if (script->result != 0)
        return script->result;
    *out = script->value_ns;
    return 0;
}

static int failures;

static void check(int condition, const char *name)
{
    if (!condition) {
        printf("FAILED %s\n", name);
        failures++;
    }
}

int main(void)
{
    scripted_clock_t script = {1000, 0};
    const periodic_clock_t clock = {scripted_now, &script};
    periodic_t state = {0, 0};
    uint64_t skipped = 99;

    check(periodic_start(&state, &clock, 100) == 0, "start");
    check(state.deadline_ns == 1100, "first deadline is one period after start");

    script.value_ns = 1100;
    check(periodic_next(&state, &clock, &skipped) == 0 && skipped == 0 && state.deadline_ns == 1200,
          "on time keeps the next period");

    /* The grid of samples/messaging/controller.c: 1350 misses 1200 and 1300. */
    state.deadline_ns = 1100;
    script.value_ns = 1350;
    check(periodic_next(&state, &clock, &skipped) == 0 && skipped == 2 && state.deadline_ns == 1400,
          "overrun skips whole periods without rebasing the phase");

    script.value_ns = 1300;
    skipped = 99;
    check(periodic_next(&state, &clock, &skipped) != 0 && skipped == 99 &&
              state.deadline_ns == 1400,
          "time before the deadline is rejected and changes nothing");

    script.result = 5;
    script.value_ns = 2000;
    check(periodic_next(&state, &clock, &skipped) != 0 && skipped == 99 &&
              state.deadline_ns == 1400,
          "clock failure changes nothing");

    script.result = 0;
    check(periodic_start(&state, &clock, 0) != 0, "zero period is rejected");
    check(periodic_start(NULL, &clock, 100) != 0, "null state is rejected");
    check(periodic_next(&state, NULL, &skipped) != 0, "null clock is rejected");

    if (failures == 0)
        puts("CONSUMER_UNIT passed");
    return failures != 0;
}
