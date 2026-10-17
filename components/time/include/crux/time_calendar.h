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
} crux_time_utc_t;

/* Convert a UTC calendar to Unix seconds; `out` changes only on success. */
crux_status_t crux_time_from_utc(const crux_time_utc_t *in, crux_time_instant_t *out);

/* Convert supported UTC Unix seconds to calendar fields; `out` changes only on success. */
crux_status_t crux_time_to_utc(crux_time_instant_t in, crux_time_utc_t *out);

#ifdef __cplusplus
}
#endif

#endif
