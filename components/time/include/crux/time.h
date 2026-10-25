/**
 * @file
 * @brief Public Crux Time API for UTC Unix seconds.
 *
 * All Time API calls must be serialised by the application. No ISR-safe or
 * re-entrant use is guaranteed.
 */
#ifndef CRUX_TIME_H
#define CRUX_TIME_H

#include <crux/status.h>

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
 * @retval CRUX_ERR_INVALID_ARGUMENT The output pointer is NULL.
 * @retval CRUX_ERR_NOT_READY The Time provider is not initialised.
 * @retval CRUX_ERR_NO_DATA Time has not been established or is no longer available.
 * @retval CRUX_ERR_OUT_OF_RANGE The provider returned a timestamp outside the supported range.
 * @retval CRUX_ERR_IO The provider read failed.
 * @return Other error status reported by the selected provider, if applicable.
 */
crux_status_t crux_time_get(crux_time_instant_t *out);

/**
 * @brief Set the UTC Unix timestamp for the current session.
 *
 * Valid timestamps lie in [CRUX_TIME_MIN_UNIX_SECONDS,
 * CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE). An invalid value is rejected before
 * provider access and preserves current validity. A failed provider write
 * invalidates session time because the underlying clock may have changed.
 *
 * @param value UTC Unix seconds in the supported interval.
 *
 * @retval CRUX_OK Time was set successfully.
 * @retval CRUX_ERR_OUT_OF_RANGE The timestamp is outside the supported interval.
 * @retval CRUX_ERR_NOT_READY The Time provider is not initialised.
 * @return Other error status reported by the selected provider on write failure.
 */
crux_status_t crux_time_set(crux_time_instant_t value);

#ifdef __cplusplus
}
#endif

#endif
