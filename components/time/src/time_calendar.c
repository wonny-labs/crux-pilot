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

crux_status_t crux_realtime_from_utc(const crux_datetime_t *datetime, crux_realtime_t *out)
{
    int64_t days = 0;
    int64_t seconds;
    int32_t year;
    int32_t month;

    if (datetime == NULL || out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (datetime->month < 1 || datetime->month > 12 || datetime->day < 1 || datetime->day > 31 ||
        datetime->hour < 0 || datetime->hour > 23 || datetime->minute < 0 ||
        datetime->minute > 59 || datetime->second < 0 || datetime->second > 59 ||
        datetime->nanoseconds >= UINT32_C(1000000000)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (datetime->day > days_in_month(datetime->year, datetime->month)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (datetime->year < 2000 || datetime->year > 2099) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    for (year = 2000; year < datetime->year; ++year) {
        days += days_in_year(year);
    }
    for (month = 1; month < datetime->month; ++month) {
        days += days_in_month(datetime->year, month);
    }
    days += datetime->day - 1;

    seconds = CRUX_TIME_MIN_UNIX_SECONDS + (days * SECONDS_PER_DAY);
    seconds += ((int64_t)datetime->hour * INT64_C(3600));
    seconds += ((int64_t)datetime->minute * INT64_C(60));
    seconds += datetime->second;

    *out = (crux_realtime_t){seconds, datetime->nanoseconds};
    return CRUX_OK;
}

crux_status_t crux_realtime_to_utc(crux_realtime_t time, crux_datetime_t *out)
{
    crux_datetime_t result;
    int64_t days;
    int64_t seconds_of_day;
    int32_t year = 2000;
    int32_t month = 1;

    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (time.nanoseconds >= UINT32_C(1000000000)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    if (time.seconds < CRUX_TIME_MIN_UNIX_SECONDS ||
        time.seconds >= CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE) {
        return CRUX_ERR_OUT_OF_RANGE;
    }

    days = (time.seconds - CRUX_TIME_MIN_UNIX_SECONDS) / SECONDS_PER_DAY;
    seconds_of_day = (time.seconds - CRUX_TIME_MIN_UNIX_SECONDS) % SECONDS_PER_DAY;

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

    result.nanoseconds = time.nanoseconds;
    *out = result;
    return CRUX_OK;
}
