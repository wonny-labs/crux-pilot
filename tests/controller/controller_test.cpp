#include "controller.h"
#include "crux_time_host_test.h"
#include "crux_time_test.h"
#include <crux/time.h>
#include <gtest/gtest.h>
#include <limits>

class ControllerTest : public ::testing::Test {
  protected:
    void SetUp() override {
        crux_time_test_reset();
        crux_time_host_test_reset();
    }
};

TEST_F(ControllerTest, InitialisationFailureIsObservableAndRetryCanRecover) {
    crux_time_host_test_fail_next_init();
    ASSERT_EQ(crux_time_init(), CRUX_ERR_IO);
    auto reading = controller_observe(0);
    EXPECT_EQ(reading.status, CRUX_ERR_NOT_READY);
    EXPECT_EQ(reading.utc_seconds, 0);
    ASSERT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(controller_observe(1).status, CRUX_ERR_NO_DATA);
}

TEST_F(ControllerTest, UnsetValidReadFailureAndRecoveryKeepCycleIdentity) {
    ASSERT_EQ(crux_time_init(), CRUX_OK);
    EXPECT_EQ(controller_observe(0).status, CRUX_ERR_NO_DATA);
    ASSERT_EQ(crux_time_set(INT64_C(1792238400)), CRUX_OK);
    auto reading = controller_observe(1);
    EXPECT_EQ(reading.utc_seconds, INT64_C(1792238400));
    EXPECT_EQ(reading.status, CRUX_OK);
    crux_time_host_test_next_read_status(CRUX_ERR_IO);
    reading = controller_observe(2);
    EXPECT_EQ(reading.cycle, 2U);
    EXPECT_EQ(reading.status, CRUX_ERR_IO);
    EXPECT_EQ(reading.utc_seconds, 0);
    EXPECT_EQ(controller_observe(3).status, CRUX_OK);
}

TEST_F(ControllerTest, UtcStepDoesNotChangeMonotonicSchedule) {
    ASSERT_EQ(crux_time_init(), CRUX_OK);
    ASSERT_EQ(crux_time_set(INT64_C(1792238400)), CRUX_OK);
    controller_schedule_t schedule = {100, 100, 99};
    ASSERT_EQ(controller_schedule_advance(&schedule, 125), CRUX_OK);
    EXPECT_EQ(schedule.deadline_ms, 200);
    ASSERT_EQ(crux_time_set(INT64_C(1792234800)), CRUX_OK);
    EXPECT_EQ(controller_observe(6).utc_seconds, INT64_C(1792234800));
    schedule.deadline_ms = 100;
    ASSERT_EQ(controller_schedule_advance(&schedule, 125), CRUX_OK);
    EXPECT_EQ(schedule.deadline_ms, 200);
}

TEST(ControllerSchedule, OverrunSkipsElapsedPhasesWithoutCatchUpBurst) {
    controller_schedule_t schedule = {100, 100, 0};
    ASSERT_EQ(controller_schedule_advance(&schedule, 350), CRUX_OK);
    EXPECT_EQ(schedule.deadline_ms, 400);
    EXPECT_EQ(schedule.skipped, 2U);
    ASSERT_EQ(controller_schedule_advance(&schedule, 400), CRUX_OK);
    EXPECT_EQ(schedule.deadline_ms, 500);
    EXPECT_EQ(schedule.skipped, 0U);
}

TEST(ControllerSchedule, InvalidAndOverflowInputsDoNotProduceADeadline) {
    controller_schedule_t schedule = {100, 10, 88};
    EXPECT_EQ(controller_schedule_advance(&schedule, 99), CRUX_ERR_INVALID_ARGUMENT);
    schedule.period_ms = 0;
    EXPECT_EQ(controller_schedule_advance(&schedule, 100), CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(controller_schedule_advance(nullptr, 100), CRUX_ERR_INVALID_ARGUMENT);
    schedule.period_ms = 10;
    EXPECT_EQ(controller_schedule_advance(&schedule, std::numeric_limits<int64_t>::max()),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(schedule.deadline_ms, 100);
    EXPECT_EQ(schedule.skipped, 88U);
}
