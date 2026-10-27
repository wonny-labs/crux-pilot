#include <crux/time_authority.h>

#include "crux_time_host_test.h"
#include "crux_time_test.h"

#include <gtest/gtest.h>
#include <thread>

static crux_status_t set_manual(crux_realtime_t value)
{
    return crux_realtime_set(&value, CRUX_REALTIME_SOURCE_MANUAL, false);
}

class TimeApiTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        crux_time_test_reset();
        crux_time_host_test_reset();
    }
};

TEST_F(TimeApiTest, InitStartsReadyButUnset)
{
    crux_realtime_t out = {123, 0};

    EXPECT_EQ(crux_realtime_init(), CRUX_OK);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(out.seconds, 123);
}

TEST_F(TimeApiTest, ArgumentAndRangeErrorsPrecedeReadiness)
{
    crux_realtime_t out = {456, 0};

    EXPECT_EQ(crux_realtime_now(nullptr), CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(set_manual(crux_realtime_t{CRUX_TIME_MIN_UNIX_SECONDS - 1, 0}),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(set_manual(crux_realtime_t{CRUX_TIME_MIN_UNIX_SECONDS, 0}), CRUX_ERR_NOT_READY);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NOT_READY);
    EXPECT_EQ(out.seconds, 456);
    EXPECT_EQ(crux_time_host_test_write_calls(), 0U);
}

TEST_F(TimeApiTest, FailedInitialisationCanBeRetried)
{
    crux_realtime_t out = {789, 0};

    crux_time_host_test_fail_next_init();
    EXPECT_EQ(crux_realtime_init(), CRUX_ERR_IO);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NOT_READY);
    EXPECT_EQ(out.seconds, 789);
    EXPECT_EQ(crux_realtime_init(), CRUX_OK);
    EXPECT_EQ(crux_realtime_init(), CRUX_OK);
    EXPECT_EQ(crux_time_host_test_init_calls(), 2U);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NO_DATA);
}

TEST_F(TimeApiTest, RepeatedInitPreservesValidTimeAndSkipsBackend)
{
    constexpr int64_t expected = 1'700'000'000;
    crux_realtime_t out = {0, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{expected, 0}), CRUX_OK);
    EXPECT_EQ(crux_realtime_init(), CRUX_OK);
    EXPECT_EQ(crux_time_host_test_init_calls(), 1U);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, expected);
}

TEST_F(TimeApiTest, RejectedWriteDoesNotReachBackendOrInvalidateTime)
{
    constexpr int64_t expected = 1'700'000'000;
    crux_realtime_t out = {0, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{expected, 0}), CRUX_OK);
    EXPECT_EQ(set_manual(crux_realtime_t{CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE, 0}),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(crux_time_host_test_write_calls(), 1U);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, expected);
}

TEST_F(TimeApiTest, FailedBackendWriteInvalidatesSessionTime)
{
    constexpr int64_t first = 1'700'000'000;
    crux_realtime_t out = {987, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{first, 0}), CRUX_OK);
    crux_time_host_test_fail_next_write();
    EXPECT_EQ(set_manual(crux_realtime_t{first + 1, 0}), CRUX_ERR_IO);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(out.seconds, 987);
    EXPECT_EQ(set_manual(crux_realtime_t{first + 2, 0}), CRUX_OK);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, first + 2);
}

TEST_F(TimeApiTest, ReadFailureLeavesOutputAndValidityUnchanged)
{
    constexpr int64_t expected = 1'700'000'000;
    crux_realtime_t out = {321, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{expected, 0}), CRUX_OK);
    crux_time_host_test_next_read_status(CRUX_ERR_IO);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_IO);
    EXPECT_EQ(out.seconds, 321);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, expected);
}

TEST_F(TimeApiTest, SuccessfulInjectedReadReturnsStoredValue)
{
    constexpr int64_t expected = 1'700'000'000;
    crux_realtime_t out = {321, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{expected, 0}), CRUX_OK);
    crux_time_host_test_next_read_status(CRUX_OK);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, expected);
}

TEST_F(TimeApiTest, BackendNoDataClearsCommonValidity)
{
    constexpr int64_t expected = 1'700'000'000;
    crux_realtime_t out = {654, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{expected, 0}), CRUX_OK);
    crux_time_host_test_next_read_status(CRUX_ERR_NO_DATA);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(out.seconds, 654);
    const unsigned int reads_after_backend_no_data = crux_time_host_test_read_calls();
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(crux_time_host_test_read_calls(), reads_after_backend_no_data);
}

TEST_F(TimeApiTest, OutOfRangeBackendReadDoesNotChangeOutput)
{
    crux_realtime_t out = {777, 0};

    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(set_manual(crux_realtime_t{CRUX_TIME_MIN_UNIX_SECONDS, 0}), CRUX_OK);
    crux_time_host_test_override_time(CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(out.seconds, 777);
}

TEST_F(TimeApiTest, TestClockAdvanceAddsExactlyFiveSeconds)
{
    constexpr int64_t start = 1'700'000'000;
    crux_realtime_t out = {0, 0};

    EXPECT_EQ(crux_time_host_test_advance(5), CRUX_ERR_NOT_READY);
    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    EXPECT_EQ(crux_time_host_test_advance(5), CRUX_ERR_NO_DATA);
    ASSERT_EQ(set_manual(crux_realtime_t{start, 0}), CRUX_OK);
    ASSERT_EQ(crux_time_host_test_advance(5), CRUX_OK);
    EXPECT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, start + 5);
}

TEST_F(TimeApiTest, FractionalMemoryTimeDoesNotProgressAndAdvancePreservesFraction)
{
    const crux_realtime_t value = {INT64_C(1700000000), 123456789};
    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(crux_realtime_set(&value, CRUX_REALTIME_SOURCE_MANUAL, false), CRUX_OK);
    for (unsigned i = 0; i < 10; ++i) {
        crux_realtime_t out;
        ASSERT_EQ(crux_realtime_now(&out), CRUX_OK);
        EXPECT_EQ(out.seconds, value.seconds);
        EXPECT_EQ(out.nanoseconds, value.nanoseconds);
    }
    ASSERT_EQ(crux_time_host_test_advance(5), CRUX_OK);
    crux_realtime_t out;
    ASSERT_EQ(crux_realtime_now(&out), CRUX_OK);
    EXPECT_EQ(out.seconds, value.seconds + 5);
    EXPECT_EQ(out.nanoseconds, value.nanoseconds);
}

TEST_F(TimeApiTest, NativeLinuxLockKeepsMemorySnapshotsConsistentAcrossWriters)
{
    const crux_realtime_t manual = {INT64_C(1700000000), 123};
    const crux_realtime_t external = {INT64_C(1792238400), 987};
    ASSERT_EQ(crux_realtime_init(), CRUX_OK);
    ASSERT_EQ(crux_realtime_set(&manual, CRUX_REALTIME_SOURCE_MANUAL, false), CRUX_OK);
    auto write = [&](bool synchronized) {
        for (unsigned i = 0; i < 5000; ++i) {
            EXPECT_EQ(crux_realtime_set(synchronized ? &external : &manual,
                                        synchronized ? CRUX_REALTIME_SOURCE_EXTERNAL
                                                     : CRUX_REALTIME_SOURCE_MANUAL,
                                        synchronized),
                      CRUX_OK);
        }
    };
    std::thread a(write, false);
    std::thread b(write, true);
    for (unsigned i = 0; i < 10000; ++i) {
        crux_realtime_snapshot_t snapshot;
        EXPECT_EQ(crux_realtime_get_snapshot(&snapshot), CRUX_OK);
        EXPECT_TRUE(snapshot.is_valid);
        const bool synchronized = snapshot.last_source == CRUX_REALTIME_SOURCE_EXTERNAL;
        const auto expected = synchronized ? external : manual;
        EXPECT_EQ(snapshot.timestamp.seconds, expected.seconds);
        EXPECT_EQ(snapshot.timestamp.nanoseconds, expected.nanoseconds);
        EXPECT_EQ(snapshot.is_synchronized, synchronized);
    }
    a.join();
    b.join();
}
