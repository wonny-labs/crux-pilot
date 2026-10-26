/**
 * @file
 * @brief Public Crux typed monotonic and legacy UTC Time API.
 *
 * Legacy realtime init/get/set calls must be serialised by the application.
 * Typed monotonic operations support concurrent thread callers with distinct
 * output storage. ISR use is unsupported.
 */
#ifndef CRUX_TIME_H
#define CRUX_TIME_H

#include <crux/status.h>
#include <crux/time_types.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** UTC timestamp in whole seconds since the Unix epoch. */
typedef int64_t crux_time_instant_t;

/** First supported UTC timestamp, inclusive (2000-01-01 00:00:00). */
#define CRUX_TIME_MIN_UNIX_SECONDS INT64_C(946684800)

/** First unsupported UTC timestamp, exclusive (2100-01-01 00:00:00). */
#define CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE INT64_C(4102444800)

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
crux_status_t crux_time_get_monotonic(crux_monotonic_time_t *out);

/** Create a deadline at least min_duration after the internal clock sample.
 * Negative duration is INVALID_ARGUMENT. Zero returns the current observation.
 * Positive duration compensates clock quantisation from the internal sample,
 * not from function return. Overflow is OUT_OF_RANGE; failure preserves output.
 * Pure crux_monotonic_add_duration() does not provide this compensation.
 * Thread checks its native absolute-wait representation separately.
 * @param min_duration Minimum nonnegative interval from the internal sample.
 * @param[out] out_deadline Assigned only on success; NULL is INVALID_ARGUMENT.
 * @return CRUX_OK, OUT_OF_RANGE, IO, INVALID_ARGUMENT or NOT_SUPPORTED in ISR.
 */
crux_status_t crux_time_make_deadline_after(crux_duration_t min_duration,
                                            crux_monotonic_time_t *out_deadline);

/**
 * @brief Initialise the selected Time provider for the current session.
 *
 * The first successful call leaves the session ready but unset. It does not
 * write a default time. Repeated calls after success preserve session validity.
 * A failed initialisation can be retried.
 *
 * @return CRUX_OK on success, otherwise the selected provider's status.
 */
crux_status_t crux_time_init(void);

/**
 * @brief Get the UTC Unix timestamp established in the current session.
 *
 * A provider read returning CRUX_ERR_NO_DATA also clears session validity.
 *
 * @param[out] out Receives whole UTC seconds on success. Unchanged on failure.
 *
 * @retval CRUX_OK A valid timestamp was returned.
 * @retval CRUX_ERR_INVALID_ARGUMENT The output is NULL or the provider reports
 *                                   an invalid argument.
 * @retval CRUX_ERR_NOT_READY The provider is not initialised or is unavailable.
 * @retval CRUX_ERR_NO_DATA Time has not been established or is no longer available.
 * @retval CRUX_ERR_OUT_OF_RANGE The observed time is outside the supported range
 *                              or the provider reports a range error.
 * @retval CRUX_ERR_IO The provider read failed.
 * @return Other error status reported by the selected provider, if applicable.
 */
crux_status_t crux_time_get(crux_time_instant_t *out);

/**
 * @brief Set the UTC Unix timestamp for the current session.
 *
 * Valid timestamps lie in [CRUX_TIME_MIN_UNIX_SECONDS,
 * CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE). An invalid value is rejected before
 * provider access and preserves current validity. A provider-side failure,
 * including a provider-reported range error, invalidates session validity
 * because the underlying clock may have changed.
 *
 * @param value UTC Unix seconds in the supported interval.
 *
 * @retval CRUX_OK Time was set successfully.
 * @retval CRUX_ERR_OUT_OF_RANGE The input is outside the supported interval,
 *                              or the provider reports a range error.
 * @retval CRUX_ERR_NOT_READY The provider is not initialised or is unavailable.
 * @return Other error status reported by the selected provider on write failure.
 */
crux_status_t crux_time_set(crux_time_instant_t value);

#ifdef __cplusplus
}
#endif

#endif
