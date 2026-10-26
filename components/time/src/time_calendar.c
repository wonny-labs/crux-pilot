#include <crux/time_calendar.h>

#include <stdbool.h>
#include <stddef.h>

#define SECONDS_PER_DAY INT64_C(86400)

static bool is_leap_year(int32_t year)
{
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

static int32_t days_in_month(int32_t year, int32_t month)
{
    static const int32_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

static int32_t days_in_year(int32_t year)
{
    return is_leap_year(year) ? 366 : 365;
}

crux_status_t crux_time_from_utc(const crux_time_utc_t *in, crux_realtime_t *out)
{
    int64_t days = 0;
    int64_t seconds;
    int32_t year;
    int32_t month;

    if (in == NULL || out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (in->month < 1 || in->month > 12 || in->day < 1 || in->day > 31 || in->hour < 0 ||
        in->hour > 23 || in->minute < 0 || in->minute > 59 || in->second < 0 || in->second > 59 ||
        in->nanoseconds >= UINT32_C(1000000000)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (in->day > days_in_month(in->year, in->month)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (in->year < 2000 || in->year > 2099) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    for (year = 2000; year < in->year; ++year) {
        days += days_in_year(year);
    }
    for (month = 1; month < in->month; ++month) {
        days += days_in_month(in->year, month);
    }
    days += in->day - 1;

    seconds = CRUX_TIME_MIN_UNIX_SECONDS + (days * SECONDS_PER_DAY);
    seconds += ((int64_t)in->hour * INT64_C(3600));
    seconds += ((int64_t)in->minute * INT64_C(60));
    seconds += in->second;

    *out = (crux_realtime_t){seconds, in->nanoseconds};
    return CRUX_OK;
}

crux_status_t crux_time_to_utc(crux_realtime_t in, crux_time_utc_t *out)
{
    crux_time_utc_t result;
    int64_t days;
    int64_t seconds_of_day;
    int32_t year = 2000;
    int32_t month = 1;

    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (in.nanoseconds >= UINT32_C(1000000000)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (in.seconds < CRUX_TIME_MIN_UNIX_SECONDS ||
        in.seconds >= CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    days = (in.seconds - CRUX_TIME_MIN_UNIX_SECONDS) / SECONDS_PER_DAY;
    seconds_of_day = (in.seconds - CRUX_TIME_MIN_UNIX_SECONDS) % SECONDS_PER_DAY;

    while (days >= days_in_year(year)) {
        days -= days_in_year(year);
        ++year;
    }
    while (days >= days_in_month(year, month)) {
        days -= days_in_month(year, month);
        ++month;
    }

    result.year = year;
    result.month = month;
    result.day = (int32_t)days + 1;
    result.hour = (int32_t)(seconds_of_day / INT64_C(3600));
    seconds_of_day %= INT64_C(3600);
    result.minute = (int32_t)(seconds_of_day / INT64_C(60));
    result.second = (int32_t)(seconds_of_day % INT64_C(60));

    result.nanoseconds = in.nanoseconds;
    *out = result;
    return CRUX_OK;
}
