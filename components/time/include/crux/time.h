#ifndef CRUX_TIME_H
#define CRUX_TIME_H

#include <crux/status.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t crux_time_instant_t;

/* UTC Unix seconds supported by this pilot; the upper bound is exclusive. */
#define CRUX_TIME_MIN_UNIX_SECONDS INT64_C(946684800)
#define CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE INT64_C(4102444800)

/*
 * Initialise the selected backend. A successful first call starts the current
 * Crux session as ready but unset; it never writes a default time. Repeated
 * successful calls preserve the session's current validity.
 *
 * Applications must serialise all Time API calls. These functions are not
 * specified as ISR-safe or re-entrant.
 */
crux_status_t crux_time_init(void);

/*
 * Read UTC Unix seconds. `out` is written only on success. Returns
 * CRUX_ERR_INVALID_ARGUMENT for a null pointer, CRUX_ERR_NOT_READY before a
 * successful init, CRUX_ERR_NO_DATA when this session has no established time,
 * CRUX_ERR_OUT_OF_RANGE if the backend supplies a value outside the pilot
 * interval, or CRUX_ERR_IO for other backend failures.
 */
crux_status_t crux_time_get(crux_time_instant_t *out);

/*
 * Set UTC Unix seconds in the supported interval. An out-of-range value is
 * rejected before backend access. Returns CRUX_ERR_NOT_READY before init and
 * the mapped backend error on failure. A failed backend write invalidates the
 * session's established time because native storage may have changed.
 */
crux_status_t crux_time_set(crux_time_instant_t value);

#ifdef __cplusplus
}
#endif

#endif
