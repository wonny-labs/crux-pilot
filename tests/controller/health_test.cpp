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
