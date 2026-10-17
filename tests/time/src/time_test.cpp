#include <crux/time.h>

#include "crux_time_host_test.h"
#include "crux_time_test.h"

#include <gtest/gtest.h>

class TimeApiTest : public ::testing::Test {
  protected:
    void SetUp() override {
        crux_time_test_reset();
        crux_time_host_test_reset();
    }
};

TEST_F(TimeApiTest, InitStartsReadyButUnset) {
    crux_time_instant_t out = 123;

    EXPECT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(out, 123);
}

TEST_F(TimeApiTest, ArgumentAndRangeErrorsPrecedeReadiness) {
    crux_time_instant_t out = 456;

    EXPECT_EQ(crux_time_get(nullptr), CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(crux_time_set(CRUX_TIME_MIN_UNIX_SECONDS - 1), CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(crux_time_set(CRUX_TIME_MIN_UNIX_SECONDS), CRUX_ERR_NOT_READY);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NOT_READY);
    EXPECT_EQ(out, 456);
    EXPECT_EQ(crux_time_host_test_write_calls(), 0U);
}

TEST_F(TimeApiTest, FailedInitialisationCanBeRetried) {
    crux_time_instant_t out = 789;

    crux_time_host_test_fail_next_init();
    EXPECT_EQ(crux_time_init(), CRUX_ERR_IO);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NOT_READY);
    EXPECT_EQ(out, 789);
    EXPECT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(crux_time_host_test_init_calls(), 2U);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NO_DATA);
}

TEST_F(TimeApiTest, RepeatedInitPreservesValidTimeAndSkipsBackend) {
    constexpr crux_time_instant_t expected = 1'700'000'000;
    crux_time_instant_t out = 0;

    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(expected), CRUX_OK);
    EXPECT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(crux_time_host_test_init_calls(), 1U);
    EXPECT_EQ(crux_time_get(&out), CRUX_OK);
    EXPECT_EQ(out, expected);
}

TEST_F(TimeApiTest, RejectedWriteDoesNotReachBackendOrInvalidateTime) {
    constexpr crux_time_instant_t expected = 1'700'000'000;
    crux_time_instant_t out = 0;

    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(expected), CRUX_OK);
    EXPECT_EQ(crux_time_set(CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE), CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(crux_time_host_test_write_calls(), 1U);
    EXPECT_EQ(crux_time_get(&out), CRUX_OK);
    EXPECT_EQ(out, expected);
}

TEST_F(TimeApiTest, FailedBackendWriteInvalidatesSessionTime) {
    constexpr crux_time_instant_t first = 1'700'000'000;
    crux_time_instant_t out = 987;

    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(first), CRUX_OK);
    crux_time_host_test_fail_next_write();
    EXPECT_EQ(crux_time_set(first + 1), CRUX_ERR_IO);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(out, 987);
    EXPECT_EQ(crux_time_set(first + 2), CRUX_OK);
    EXPECT_EQ(crux_time_get(&out), CRUX_OK);
    EXPECT_EQ(out, first + 2);
}

TEST_F(TimeApiTest, ReadFailureLeavesOutputAndValidityUnchanged) {
    constexpr crux_time_instant_t expected = 1'700'000'000;
    crux_time_instant_t out = 321;

    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(expected), CRUX_OK);
    crux_time_host_test_next_read_status(CRUX_ERR_IO);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_IO);
    EXPECT_EQ(out, 321);
    EXPECT_EQ(crux_time_get(&out), CRUX_OK);
    EXPECT_EQ(out, expected);
}

TEST_F(TimeApiTest, BackendNoDataClearsCommonValidity) {
    constexpr crux_time_instant_t expected = 1'700'000'000;
    crux_time_instant_t out = 654;

    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(expected), CRUX_OK);
    crux_time_host_test_next_read_status(CRUX_ERR_NO_DATA);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(out, 654);
    const unsigned int reads_after_backend_no_data = crux_time_host_test_read_calls();
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_NO_DATA);
    EXPECT_EQ(crux_time_host_test_read_calls(), reads_after_backend_no_data);
}

TEST_F(TimeApiTest, OutOfRangeBackendReadDoesNotChangeOutput) {
    crux_time_instant_t out = 777;

    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(CRUX_TIME_MIN_UNIX_SECONDS), CRUX_OK);
    crux_time_host_test_override_time(CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE);
    EXPECT_EQ(crux_time_get(&out), CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(out, 777);
}

TEST_F(TimeApiTest, TestClockAdvanceAddsExactlyFiveSeconds) {
    constexpr crux_time_instant_t start = 1'700'000'000;
    crux_time_instant_t out = 0;

    EXPECT_EQ(crux_time_host_test_advance(5), CRUX_ERR_NOT_READY);
    ASSERT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(crux_time_host_test_advance(5), CRUX_ERR_NO_DATA);
    ASSERT_EQ(crux_time_set(start), CRUX_OK);
    ASSERT_EQ(crux_time_host_test_advance(5), CRUX_OK);
    EXPECT_EQ(crux_time_get(&out), CRUX_OK);
    EXPECT_EQ(out, start + 5);
}
