#ifndef CRUX_TIME_CALENDAR_H
#define CRUX_TIME_CALENDAR_H

#include <crux/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* Proleptic Gregorian calendar fields in UTC; seconds are 0 through 59. */
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t hour;
    int32_t minute;
    int32_t second;
    uint32_t nanoseconds;
} crux_time_utc_t;

/** Convert Gregorian UTC to normalized POSIX realtime, preserving nanoseconds.
 * Pure conversion; no init, clock, lock or allocation. Leap seconds unsupported.
 * @param in Calendar fields; nanoseconds must be less than 1000000000.
 * @param[out] out Assigned only on success; caller owns both objects.
 * @return OK, INVALID_ARGUMENT for NULL/invalid fields, OUT_OF_RANGE for a
 * valid calendar outside years 2000 through 2099. Reentrant with distinct outputs.
 */
crux_status_t crux_time_from_utc(const crux_time_utc_t *in, crux_realtime_t *out);

/** Convert normalized POSIX realtime to Gregorian UTC, preserving nanoseconds.
 * @param in Time in [2000-01-01, 2100-01-01), with normalized nanoseconds.
 * @param[out] out Assigned only on success; NULL is INVALID_ARGUMENT.
 * @return OK, INVALID_ARGUMENT for malformed nanoseconds/NULL, OUT_OF_RANGE
 * for unsupported seconds. Pure, reentrant, no init or native state required.
 */
crux_status_t crux_time_to_utc(crux_realtime_t in, crux_time_utc_t *out);

#ifdef __cplusplus
}
#endif

#endif
