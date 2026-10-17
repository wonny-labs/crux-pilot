#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <crux/status.h>
#include <stdint.h>

/* Sample-owned declarations, not a supported Crux platform API.
 * The composition serialises Time calls and supplies monotonic milliseconds. */
typedef struct {
    uint32_t cycle;
    crux_status_t status;
    int64_t utc_seconds; /* Meaningful only when status == CRUX_OK. */
} controller_reading_t;

typedef struct {
    int64_t deadline_ms;
    int64_t period_ms;
    uint64_t skipped;
} controller_schedule_t;

#ifdef __cplusplus
extern "C" {
#endif
controller_reading_t controller_observe(uint32_t cycle);
crux_status_t controller_schedule_advance(controller_schedule_t *schedule, int64_t now_ms);
#ifdef __cplusplus
}
#endif
#endif
