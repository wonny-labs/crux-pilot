#ifndef CRUX_THREAD_H
#define CRUX_THREAD_H

#include <crux/status.h>
#include <crux/time_types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reserved value representation. Zero is invalid; do not construct or inspect handles. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_thread_t;
typedef enum {
    CRUX_THREAD_PRIORITY_LOW = 0,
    CRUX_THREAD_PRIORITY_NORMAL = 1,
    CRUX_THREAD_PRIORITY_HIGH = 2
} crux_thread_priority_t;

typedef struct {
    /* Minimum usable bytes. Zero requests the provider default. */
    size_t stack_size_bytes;
    crux_thread_priority_t priority;
} crux_thread_options_t;

typedef void (*crux_thread_entry_t)(void *argument);

/* Thread context only. Waits: 0 polls; 1..INT32_MAX are monotonic relative ms.
 * No cancellation/ISR/forever support. See docs/thread-sync.md
 * for the current contract, provider limits and historical evidence.
 * Output storage must not hold a live resource. Failure invalidates create outputs.
 * Join timeout retains the lifetime; only successful join releases stack/argument ownership.
 * Callers serialise handle-variable writes and prohibit new admissions during destroy.
 */
crux_status_t crux_thread_create(const crux_thread_options_t *options, crux_thread_entry_t entry,
                                 void *argument, crux_thread_t *out);
crux_status_t crux_thread_join(crux_thread_t *thread, uint32_t timeout_ms);

/** Sleep the current thread for a nonnegative duration.
 * Independent of realtime/RTC initialization. Negative duration is
 * INVALID_ARGUMENT; ISR is NOT_SUPPORTED.
 * Zero relative sleep succeeds immediately without yielding. Absolute deadlines
 * at/before now are immediate. Early wake/interrupt retries the original deadline;
 * success requires a final observation >= deadline. No cancellation or maximum
 * return latency is promised. Native representation overflow is OUT_OF_RANGE;
 * the former INT32_MAX-millisecond sleep limit does not apply.
 * @param duration Nonnegative minimum interval from the internal clock sample.
 * @return CRUX_OK or the argument/context/range/native clock or wait error.
 */
crux_status_t crux_thread_sleep_for(crux_duration_t duration);
/** Wait until the original absolute monotonic deadline is observed.
 * Negative deadline is INVALID_ARGUMENT; ISR is NOT_SUPPORTED. Past/current
 * deadlines succeed without waiting. Early wake rechecks this same deadline;
 * native representation overflow is OUT_OF_RANGE. No extra tick padding is
 * applied here. Scheduling latency is unbounded and cancellation unsupported.
 * @param deadline A valid timestamp from this provider and execution session.
 * @return CRUX_OK or the argument/context/range/native clock or wait error.
 */
crux_status_t crux_thread_sleep_until(crux_monotonic_time_t deadline);

#ifdef __cplusplus
}
#endif

#endif
