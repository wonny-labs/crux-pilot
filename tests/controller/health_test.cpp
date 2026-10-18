#include "controller_health.h"

#include <gtest/gtest.h>
#include <limits>

TEST(ControllerHealth, RequiresCompletedWorkAndExpiresOnActualCompletionTime) {
    controller_health_t health;
    ASSERT_TRUE(controller_health_init(&health, 500));
    EXPECT_FALSE(controller_health_observe(&health, 0, controller_progress_t{-1, 0, false}));
    EXPECT_TRUE(controller_health_observe(&health, 100, controller_progress_t{100, 1, true}));
    EXPECT_TRUE(controller_health_observe(&health, 600, controller_progress_t{100, 1, true}));
    EXPECT_FALSE(controller_health_observe(&health, 601, controller_progress_t{100, 1, true}));
    // Delayed observation must not rejuvenate old, newly noticed work.
    EXPECT_FALSE(controller_health_observe(&health, 2000, controller_progress_t{200, 2, true}));
    EXPECT_TRUE(controller_health_observe(&health, 2100, controller_progress_t{2100, 3, true}));
}

TEST(ControllerHealth, ErrorAndFutureCompletionNeedNewSuccessfulProgress) {
    controller_health_t health;
    ASSERT_TRUE(controller_health_init(&health, 500));
    ASSERT_TRUE(controller_health_observe(&health, 100, controller_progress_t{100, 1, true}));
    EXPECT_FALSE(controller_health_observe(&health, 200, controller_progress_t{200, 2, false}));
    EXPECT_FALSE(controller_health_observe(&health, 300, controller_progress_t{200, 2, true}));
    EXPECT_FALSE(controller_health_observe(&health, 400, controller_progress_t{500, 3, true}));
    EXPECT_FALSE(controller_health_observe(&health, 500, controller_progress_t{500, 3, true}));
    EXPECT_TRUE(controller_health_observe(&health, 600, controller_progress_t{600, 4, true}));
}

TEST(ControllerHealth, InvalidOrBackwardTimeCannotFeedOrRenewProgress) {
    controller_health_t health;
    EXPECT_FALSE(controller_health_init(nullptr, 500));
    EXPECT_FALSE(controller_health_init(&health, 0));
    ASSERT_TRUE(controller_health_init(&health, 500));
    EXPECT_FALSE(controller_health_observe(nullptr, 0, controller_progress_t{0, 1, true}));
    ASSERT_TRUE(controller_health_observe(&health, 100, controller_progress_t{100, 1, true}));
    EXPECT_FALSE(controller_health_observe(&health, 99, controller_progress_t{99, 2, true}));
    EXPECT_EQ(health.last_progress_ms, 100);
    EXPECT_FALSE(controller_health_observe(&health, 200, controller_progress_t{100, 1, true}));
    EXPECT_TRUE(controller_health_observe(&health, 300, controller_progress_t{300, 3, true}));
    EXPECT_FALSE(controller_health_observe(&health, 400, controller_progress_t{299, 4, true}));
    EXPECT_FALSE(controller_health_observe(&health, -1, controller_progress_t{100, 3, true}));
    EXPECT_FALSE(controller_health_observe(&health, 500, controller_progress_t{300, 3, true}));
    EXPECT_TRUE(controller_health_observe(&health, 600, controller_progress_t{600, 5, true}));
}

TEST(ControllerHealth, RecoveryCannotReplayWorkCompletedBeforeTheError) {
    controller_health_t health;
    ASSERT_TRUE(controller_health_init(&health, 500));
    ASSERT_TRUE(controller_health_observe(&health, 100, controller_progress_t{100, 1, true}));
    EXPECT_FALSE(controller_health_observe(&health, 200, controller_progress_t{200, 2, false}));
    EXPECT_FALSE(controller_health_observe(&health, 300, controller_progress_t{150, 3, true}));
    EXPECT_TRUE(controller_health_observe(&health, 400, controller_progress_t{400, 4, true}));
}

TEST(ControllerHealth, CounterWrapUsesChangeRatherThanNumericOrdering) {
    controller_health_t health;
    ASSERT_TRUE(controller_health_init(&health, 500));
    ASSERT_TRUE(controller_health_observe(
        &health, 100, controller_progress_t{100, std::numeric_limits<uint32_t>::max(), true}));
    EXPECT_TRUE(controller_health_observe(&health, 200, controller_progress_t{200, 0, true}));
    EXPECT_FALSE(controller_health_observe(&health, 701, controller_progress_t{200, 0, true}));
}

TEST(ControllerReadiness, RequiredFailureCannotBecomeReadyOrOptionalDegradation) {
    EXPECT_EQ(controller_readiness(false, true, true, true), CONTROLLER_NOT_READY);
    EXPECT_EQ(controller_readiness(true, false, true, true), CONTROLLER_NOT_READY);
    EXPECT_EQ(controller_readiness(true, true, false, true), CONTROLLER_NOT_READY);
    EXPECT_EQ(controller_readiness(true, true, true, false), CONTROLLER_DEGRADED);
    EXPECT_EQ(controller_readiness(true, true, true, true), CONTROLLER_READY);
}

TEST(ControllerJointHealth, EitherOwnerMustAdvanceAndErrorsNeedNewWork) {
    controller_health_t time_health, network_health;
    ASSERT_TRUE(controller_health_init(&time_health, 500));
    ASSERT_TRUE(controller_health_init(&network_health, 16000));
    auto observe = [&](int64_t now, controller_progress_t t, controller_progress_t n) {
        return controller_health_joint_observe(&time_health, &network_health, now, t, n);
    };
    EXPECT_TRUE(observe(100, {100, 1, true}, {100, 1, true}));
    // The network owner can still advance while stopped Time work prevents feeding.
    EXPECT_FALSE(observe(701, {100, 1, true}, {701, 2, true}));
    EXPECT_EQ(network_health.last_progress_ms, 701);
    EXPECT_TRUE(observe(800, {800, 2, true}, {701, 2, true}));
    // Fresh Time work cannot hide a stopped network owner beyond its own allowance.
    EXPECT_TRUE(observe(16701, {16701, 3, true}, {701, 2, true}));
    EXPECT_FALSE(observe(16702, {16702, 4, true}, {701, 2, true}));
    EXPECT_FALSE(observe(16800, {16800, 5, true}, {16800, 3, false}));
    EXPECT_FALSE(observe(16900, {16900, 6, true}, {16750, 4, true}));
    EXPECT_TRUE(observe(17000, {17000, 7, true}, {17000, 5, true}));
    EXPECT_FALSE(observe(17100, {17100, 8, true}, {17200, 6, true}));
    EXPECT_FALSE(observe(17200, {17200, 9, true}, {17200, 6, true}));
    EXPECT_TRUE(observe(17300, {17300, 10, true}, {17300, 7, true}));
}

TEST(ControllerM3Budget, ArmsOnlyAfterJointPreparationWithFaultAndResetRoom) {
    EXPECT_FALSE(controller_m3_arm_allowed(-1, 0, 60000));
    EXPECT_FALSE(controller_m3_arm_allowed(1000, -1, 60000));
    EXPECT_FALSE(controller_m3_arm_allowed(1000, 1001, 60000));
    EXPECT_FALSE(controller_m3_arm_allowed(999, 0, 60000));
    EXPECT_TRUE(controller_m3_arm_allowed(1000, 0, 41000));
    EXPECT_FALSE(controller_m3_arm_allowed(1000, 0, 40999));
    EXPECT_FALSE(controller_m3_arm_allowed(60001, 0, 60000));
    EXPECT_TRUE(controller_m3_fault_allowed(1000, 16000, 26000));
    EXPECT_FALSE(controller_m3_fault_allowed(1000, 16000, 25999));
    EXPECT_FALSE(controller_m3_fault_allowed(16000, 16000, 60000));
    EXPECT_FALSE(controller_m3_fault_allowed(-1, 16000, 60000));
    EXPECT_FALSE(controller_m3_fault_allowed(61000, 62000, 60000));
}
