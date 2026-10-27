/** @file
 * @brief Explicit authority operations for system realtime, not RTC storage.
 *
 * All operations require thread context and share the observation lock. Do not
 * re-enter Time while a provider holds that lock. This header separates roles,
 * not runtime permissions. Applications own stale-callback and source policy.
 */
#ifndef CRUX_TIME_AUTHORITY_H
#define CRUX_TIME_AUTHORITY_H

#include <crux/time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Adopt the current native realtime without writing it.
 * Success establishes valid, unsynchronized time with source UNKNOWN.
 * @return OK, NOT_READY before init, NOT_SUPPORTED in ISR, or read errors as
 * documented by crux_realtime_now. Failed adoption never establishes validity.
 */
crux_status_t crux_realtime_adopt(void);

/** Set system realtime and authority metadata atomically for Crux observers.
 * @param time Normalized time in [2000-01-01, 2100-01-01); never retained.
 * @param source RTC, MANUAL or EXTERNAL. NONE/UNKNOWN are invalid arguments.
 * @param actively_synchronized May be true only with source EXTERNAL.
 * Invalid arguments/range are rejected before state/native access. An actual
 * native write failure clears validity/sync while retaining the previous source.
 * Success does not write RTC storage or promise quantitative accuracy.
 * @return OK, INVALID_ARGUMENT, OUT_OF_RANGE, NOT_READY, NOT_SUPPORTED in ISR,
 * or the native write error. Provider lock failure leaves state unchanged.
 */
crux_status_t crux_realtime_set(const crux_realtime_t *time, crux_realtime_source_t source,
                                bool actively_synchronized);

/** Update synchronization metadata without writing realtime.
 * @param actively_synchronized True requires a valid EXTERNAL source.
 * @return OK, NO_DATA when invalid, INVALID_STATE for true with non-EXTERNAL
 * source, NOT_READY before init, NOT_SUPPORTED in ISR, or lock failure.
 */
crux_status_t crux_realtime_set_synchronized(bool actively_synchronized);

/** Clear validity and synchronization, retaining last source; no native write.
 * @return OK, NOT_READY before init, NOT_SUPPORTED in ISR, or lock failure.
 */
crux_status_t crux_realtime_invalidate(void);

#ifdef __cplusplus
}
#endif
#endif
