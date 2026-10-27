/**
 * @file
 * @brief Public Crux typed clocks and optional CPU Busy-wait.
 *
 * Realtime observations and authority operations share a private native lock.
 * Concurrent callers use distinct output storage. Clock/realtime operations require
 * thread context; Busy-wait has separate provider/context limits below.
 */
#ifndef CRUX_TIME_H
#define CRUX_TIME_H

#include <crux/status.h>
#include <crux/time_types.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** First supported UTC timestamp, inclusive (2000-01-01 00:00:00). */
#define CRUX_TIME_MIN_UNIX_SECONDS INT64_C(946684800)

/** First unsupported UTC timestamp, exclusive (2100-01-01 00:00:00). */
#define CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE INT64_C(4102444800)

/** Origin of the last successfully established realtime value; not accuracy. */
typedef enum {
    CRUX_REALTIME_SOURCE_NONE = 0,
    CRUX_REALTIME_SOURCE_UNKNOWN = 1,
    CRUX_REALTIME_SOURCE_RTC = 2,
    CRUX_REALTIME_SOURCE_MANUAL = 3,
    CRUX_REALTIME_SOURCE_EXTERNAL = 4
} crux_realtime_source_t;

/** One consistent observation of realtime and its authority metadata. */
typedef struct {
    crux_realtime_t timestamp;
    crux_realtime_source_t last_source;
    bool is_valid;        /**< A supported realtime value has been established. */
    bool is_synchronized; /**< Currently reported active synchronization, not accuracy. */
} crux_realtime_snapshot_t;

/** Read established realtime, in thread context, under the authority lock.
 * @param[out] out Assigned only on success. NULL is INVALID_ARGUMENT.
 * @return OK, NOT_READY before init, NO_DATA when invalid, OUT_OF_RANGE for an
 * unsupported native observation, IO for a native failure, or NOT_SUPPORTED in
 * ISR. NO_DATA/range failures invalidate time and synchronization; other read
 * failures preserve state. Native setters outside Crux are not synchronized.
 */
crux_status_t crux_realtime_now(crux_realtime_t *out);

/** Read time and metadata as one Crux transaction in thread context.
 * Invalid time produces an OK snapshot with zero timestamp and retained source,
 * without reading the native clock. Native NO_DATA produces that same result.
 * @param[out] out Assigned only on success; NULL is INVALID_ARGUMENT.
 * @return OK, NOT_READY, OUT_OF_RANGE, IO or NOT_SUPPORTED as for crux_realtime_now.
 */
crux_status_t crux_realtime_get_snapshot(crux_realtime_snapshot_t *out);

/** Read the provider/session-local monotonic clock.
 * Thread context; independent of realtime initialisation, validity and RTC.
 * Nonnegative provider/session-local nanoseconds; repeated values are permitted.
 * Representation does not guarantee resolution, suspend continuity or accuracy.
 * Concurrent callers use distinct output storage. NULL is INVALID_ARGUMENT,
 * ISR is NOT_SUPPORTED. Failure preserves *out.
 * @param[out] out Nonnegative observation in nanoseconds, assigned on success.
 * @return CRUX_OK, OUT_OF_RANGE for an unrepresentable sample, IO for native
 * failure, or the argument/context error documented above.
 */
crux_status_t crux_monotonic_now(crux_monotonic_time_t *out);

/** Create a deadline at least min_duration after the internal clock sample.
 * Negative duration is INVALID_ARGUMENT. Zero returns the current observation.
 * Positive duration compensates clock quantisation from the internal sample,
 * not from function return. Overflow is OUT_OF_RANGE; failure preserves output.
 * Pure crux_monotonic_add() does not provide this compensation.
 * Thread checks its native absolute-wait representation separately.
 * @param min_duration Minimum nonnegative interval from the internal sample.
 * @param[out] out Assigned only on success; NULL is INVALID_ARGUMENT.
 * @return CRUX_OK, OUT_OF_RANGE, IO, INVALID_ARGUMENT or NOT_SUPPORTED in ISR.
 */
crux_status_t crux_monotonic_deadline_after(crux_duration_t min_duration,
                                            crux_monotonic_time_t *out);

/**
 * @brief Prepare the selected realtime provider for the current session.
 *
 * The first successful call leaves the session ready but unset. It does not
 * write a default time. Repeated calls after success preserve session validity.
 * A failed initialisation can be retried. It never adopts native time or loads
 * RTC. Concurrent thread calls share the authority lock. Monotonic needs no init.
 *
 * @return CRUX_OK, NOT_SUPPORTED in ISR/unsupported clock configuration, or a
 * native lock/provider initialization error.
 */
crux_status_t crux_realtime_init(void);

/** CPU busy-wait without Crux locks, allocation or scheduler blocking.
 * @param duration Negative returns INVALID_ARGUMENT; zero succeeds immediately
 * without native clock access or yielding. Positive disabled requests return
 * NOT_SUPPORTED. Enabled providers validate safe input bounds before waiting,
 * round native units up, and return OUT_OF_RANGE rather than wrap or clamp.
 * Native errors remain provider errors. No public maximum or latency guarantee.
 * Preserves caller IRQ state; brief native interrupt-masked observations are
 * permitted, but interrupts are not masked for the requested delay. ISR use
 * requires validated provider/target configuration; no general lock-free claim.
 * @return OK, INVALID_ARGUMENT, NOT_SUPPORTED, OUT_OF_RANGE or IO.
 */
crux_status_t crux_busy_wait(crux_duration_t duration);

#ifdef __cplusplus
}
#endif

#endif
