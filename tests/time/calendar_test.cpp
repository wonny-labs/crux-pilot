#include <crux/time_calendar.h>

#include <gtest/gtest.h>

TEST(TimeCalendar, ConvertsIndependentUnixEpochAnchor)
{
    const crux_time_utc_t epoch{2000, 1, 1, 0, 0, 0, 0};
    crux_realtime_t instant = {-1, 0};
    crux_time_utc_t calendar{};

    ASSERT_EQ(crux_time_from_utc(&epoch, &instant), CRUX_OK);
    EXPECT_EQ(instant.seconds, INT64_C(946684800));
    ASSERT_EQ(crux_time_to_utc(crux_realtime_t{INT64_C(946684800), 0}, &calendar), CRUX_OK);
    EXPECT_EQ(calendar.year, 2000);
    EXPECT_EQ(calendar.month, 1);
    EXPECT_EQ(calendar.day, 1);
    EXPECT_EQ(calendar.hour, 0);
    EXPECT_EQ(calendar.minute, 0);
    EXPECT_EQ(calendar.second, 0);
}

TEST(TimeCalendar, ConvertsIndependentPost2038AndUpperRangeAnchors)
{
    struct anchor {
        crux_time_utc_t calendar;
        int64_t unix_seconds;
    };
    const anchor anchors[] = {
        {{2038, 1, 19, 3, 14, 8, 0}, INT64_C(2147483648)},
        {{2099, 12, 31, 23, 59, 59, 0}, INT64_C(4102444799)},
    };

    for (const anchor &value : anchors) {
        crux_realtime_t instant = {0, 0};
        crux_time_utc_t calendar{};
        ASSERT_EQ(crux_time_from_utc(&value.calendar, &instant), CRUX_OK);
        EXPECT_EQ(instant.seconds, value.unix_seconds);
        ASSERT_EQ(crux_time_to_utc(crux_realtime_t{value.unix_seconds, 0}, &calendar), CRUX_OK);
        EXPECT_EQ(calendar.year, value.calendar.year);
        EXPECT_EQ(calendar.month, value.calendar.month);
        EXPECT_EQ(calendar.day, value.calendar.day);
        EXPECT_EQ(calendar.hour, value.calendar.hour);
        EXPECT_EQ(calendar.minute, value.calendar.minute);
        EXPECT_EQ(calendar.second, value.calendar.second);
    }
}

TEST(TimeCalendar, EnforcesInclusiveStartAndExclusiveEnd)
{
    crux_time_utc_t calendar{1999, 12, 31, 23, 59, 59, 0};
    crux_realtime_t instant = {0, 0};
    crux_time_utc_t out{1, 2, 3, 4, 5, 6, 0};

    EXPECT_EQ(crux_time_from_utc(&calendar, &instant), CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(instant.seconds, 0);
    calendar = {2000, 1, 1, 0, 0, 0, 0};
    EXPECT_EQ(crux_time_from_utc(&calendar, &instant), CRUX_OK);
    EXPECT_EQ(instant.seconds, CRUX_TIME_MIN_UNIX_SECONDS);
    EXPECT_EQ(crux_time_to_utc(crux_realtime_t{CRUX_TIME_MIN_UNIX_SECONDS - 1, 0}, &out),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(out.year, 1);
    EXPECT_EQ(crux_time_to_utc(crux_realtime_t{CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE - 1, 0}, &out),
              CRUX_OK);
    EXPECT_EQ(out.year, 2099);
    EXPECT_EQ(out.month, 12);
    EXPECT_EQ(out.day, 31);
    EXPECT_EQ(crux_time_to_utc(crux_realtime_t{CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE, 0}, &out),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(out.year, 2099);
}

TEST(TimeCalendar, AcceptsGregorianLeapDaysAndRejectsInvalidOnes)
{
    crux_time_utc_t calendar{2000, 2, 29, 12, 0, 0, 0};
    crux_realtime_t instant = {0, 0};

    EXPECT_EQ(crux_time_from_utc(&calendar, &instant), CRUX_OK);
    calendar.year = 2004;
    EXPECT_EQ(crux_time_from_utc(&calendar, &instant), CRUX_OK);
    calendar.year = 2001;
    EXPECT_EQ(crux_time_from_utc(&calendar, &instant), CRUX_ERR_INVALID_ARGUMENT);
    calendar.year = 2100;
    EXPECT_EQ(crux_time_from_utc(&calendar, &instant), CRUX_ERR_INVALID_ARGUMENT);
}

TEST(TimeCalendar, RejectsInvalidFieldsAndPreservesOutput)
{
    const crux_time_utc_t invalid[] = {
        {2000, 0, 1, 0, 0, 0, 0},  {2000, 13, 1, 0, 0, 0, 0}, {2000, 1, 0, 0, 0, 0, 0},
        {2000, 1, 32, 0, 0, 0, 0}, {2001, 2, 29, 0, 0, 0, 0}, {2000, 2, 30, 0, 0, 0, 0},
        {2000, 1, 1, -1, 0, 0, 0}, {2000, 1, 1, 24, 0, 0, 0}, {2000, 1, 1, 0, -1, 0, 0},
        {2000, 1, 1, 0, 60, 0, 0}, {2000, 1, 1, 0, 0, -1, 0}, {2000, 1, 1, 0, 0, 60, 0},
    };

    for (const crux_time_utc_t &calendar : invalid) {
        crux_realtime_t out = {12345, 0};
        EXPECT_EQ(crux_time_from_utc(&calendar, &out), CRUX_ERR_INVALID_ARGUMENT);
        EXPECT_EQ(out.seconds, 12345);
    }
}

TEST(TimeCalendar, NullArgumentsAreInvalid)
{
    const crux_time_utc_t calendar{2000, 1, 1, 0, 0, 0, 0};
    crux_realtime_t instant = {9, 0};
    crux_time_utc_t out{1, 2, 3, 4, 5, 6, 0};

    EXPECT_EQ(crux_time_from_utc(nullptr, &instant), CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(crux_time_from_utc(&calendar, nullptr), CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(instant.seconds, 9);
    EXPECT_EQ(crux_time_to_utc(crux_realtime_t{CRUX_TIME_MIN_UNIX_SECONDS, 0}, nullptr),
              CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(crux_time_to_utc(crux_realtime_t{CRUX_TIME_MIN_UNIX_SECONDS - 1, 0}, &out),
              CRUX_ERR_OUT_OF_RANGE);
}

TEST(TimeCalendar, RoundTripsMonthBoundariesAndLeapDay)
{
    const crux_time_utc_t cases[] = {
        {2000, 2, 29, 23, 59, 59, 0}, {2000, 3, 1, 0, 0, 0, 0},      {2038, 1, 19, 3, 14, 7, 0},
        {2099, 1, 1, 0, 0, 0, 0},     {2099, 12, 31, 23, 59, 59, 0},
    };

    for (const crux_time_utc_t &value : cases) {
        crux_realtime_t instant = {0, 0};
        crux_time_utc_t actual{};
        ASSERT_EQ(crux_time_from_utc(&value, &instant), CRUX_OK);
        ASSERT_EQ(crux_time_to_utc(instant, &actual), CRUX_OK);
        EXPECT_EQ(actual.year, value.year);
        EXPECT_EQ(actual.month, value.month);
        EXPECT_EQ(actual.day, value.day);
        EXPECT_EQ(actual.hour, value.hour);
        EXPECT_EQ(actual.minute, value.minute);
        EXPECT_EQ(actual.second, value.second);
    }
}
