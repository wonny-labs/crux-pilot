#include <crux/time_calendar.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
int main(void) {
    const crux_time_utc_t values[] = {{2000, 1, 1, 0, 0, 0, 1},
                                      {2000, 2, 29, 23, 59, 59, 123456789},
                                      {2038, 1, 19, 3, 14, 8, 987654321},
                                      {2099, 12, 31, 23, 59, 59, 999999999}};
    const int64_t seconds[] = {946684800, 951868799, 2147483648, 4102444799};
    for (unsigned i = 0; i < 4; ++i) {
        crux_realtime_t timestamp = {0};
        crux_time_utc_t actual = {0};
        CHECK(crux_time_from_utc(&values[i], &timestamp) == CRUX_OK);
        CHECK(timestamp.seconds == seconds[i] && timestamp.nanoseconds == values[i].nanoseconds);
        CHECK(crux_time_to_utc(timestamp, &actual) == CRUX_OK);
        CHECK(actual.year == values[i].year && actual.month == values[i].month &&
              actual.day == values[i].day && actual.hour == values[i].hour &&
              actual.minute == values[i].minute && actual.second == values[i].second &&
              actual.nanoseconds == values[i].nanoseconds);
    }
    crux_time_utc_t bad = values[0];
    bad.nanoseconds = 1000000000;
    crux_realtime_t timestamp = {42, 73};
    CHECK(crux_time_from_utc(&bad, &timestamp) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(timestamp.seconds == 42 && timestamp.nanoseconds == 73);
    crux_time_utc_t calendar, sentinel;
    memset(&calendar, 0x55, sizeof(calendar));
    sentinel = calendar;
    const crux_realtime_t invalid[] = {
        {946684800, 1000000000}, {946684800, UINT32_MAX}, {946684799, 999999999}, {4102444800, 0}};
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(crux_time_to_utc(invalid[i], &calendar) ==
              (i < 2 ? CRUX_ERR_INVALID_ARGUMENT : CRUX_ERR_OUT_OF_RANGE));
        CHECK(memcmp(&calendar, &sentinel, sizeof(calendar)) == 0);
    }
    CHECK(crux_time_from_utc(NULL, &timestamp) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_from_utc(&values[0], NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_to_utc(timestamp, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    printf("fractional Gregorian UTC: %d failures\n", failures);
    return failures != 0;
}
