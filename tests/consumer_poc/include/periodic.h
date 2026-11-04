/* Application-owned periodic release policy and its clock port. Crux supplies no
 * fake clock: this consumer owns the port and writes its double in its own tests. */
#ifndef CONSUMER_PERIODIC_H
#define CONSUMER_PERIODIC_H

#include <stdint.h>

typedef struct {
    int (*now_ns)(void *context, int64_t *out);
    void *context;
} periodic_clock_t;

typedef struct {
    int64_t period_ns;
    int64_t deadline_ns;
} periodic_t;

/* Returns 0, or nonzero for invalid arguments or a clock failure. */
int periodic_start(periodic_t *state, const periodic_clock_t *clock, int64_t period_ns);

/* Move the deadline past the current time on the original phase and report the
 * whole periods that were missed. Output and state are unchanged on failure. */
int periodic_next(periodic_t *state, const periodic_clock_t *clock, uint64_t *skipped);

#endif
