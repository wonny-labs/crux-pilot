/* Portable application scenario. It reports through this port, so each composition
 * chooses where its output goes. */
#ifndef CONSUMER_APP_RUN_H
#define CONSUMER_APP_RUN_H

#include <stdint.h>

#define APP_CYCLES 6
#define APP_OVERRUN_CYCLE 3
/* An overrun of 2.5 periods misses exactly two releases on simulated time. */
#define APP_OVERRUN_SKIPS 2

typedef void (*app_report_t)(void *context, int cycle, uint64_t skipped);

/* Returns 0, or nonzero on a Crux or policy failure. */
int app_run(app_report_t report, void *context);

#endif
