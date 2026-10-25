#include <crux/time_types.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

extern "C" int crux_time_types_c_smoke(void);

TEST(TimeTypes, CAndCppConsumersCompileAndLink) {
    EXPECT_EQ(crux_time_types_c_smoke(), 1);
    EXPECT_FALSE((std::is_same<crux_duration_t, crux_monotonic_time_t>::value));
    EXPECT_FALSE((std::is_same<crux_duration_t, crux_realtime_t>::value));
}

TEST(TimeTypes, SignedSafeConstructorsAndStaticInitialisers) {
    static const crux_duration_t negative_static = CRUX_DURATION_SEC_INIT(-2);
    EXPECT_EQ(negative_static.private_ns, -INT64_C(2000000000));
    EXPECT_EQ(crux_duration_from_ns(-1).private_ns, -1);
    EXPECT_EQ(crux_duration_from_us(-3).private_ns, -3000);
    EXPECT_EQ(crux_duration_from_ms(-3).private_ns, -3000000);
    EXPECT_EQ(crux_duration_from_sec(-3).private_ns, -INT64_C(3000000000));
    EXPECT_EQ(crux_duration_from_sec(INT32_MAX).private_ns,
              (int64_t)INT32_MAX * INT64_C(1000000000));
    EXPECT_EQ(crux_duration_from_sec(INT32_MIN).private_ns,
              (int64_t)INT32_MIN * INT64_C(1000000000));
}

TEST(TimeTypes, CheckedConstructorsAllowNegativeAndPreserveOutput) {
    const struct {
        int64_t factor;
        crux_status_t (*create)(int64_t, crux_duration_t *);
    } cases[] = {
        {1000, crux_duration_try_from_us},
        {1000000, crux_duration_try_from_ms},
        {1000000000, crux_duration_try_from_sec},
    };
    for (const auto &entry : cases) {
        crux_duration_t result{12345};
        EXPECT_EQ(entry.create(-2, &result), CRUX_OK);
        EXPECT_EQ(result.private_ns, -2 * entry.factor);
        EXPECT_EQ(entry.create(INT64_MAX / entry.factor, &result), CRUX_OK);
        EXPECT_EQ(entry.create(INT64_MAX / entry.factor + 1, &result), CRUX_ERR_OUT_OF_RANGE);
        EXPECT_EQ(result.private_ns, (INT64_MAX / entry.factor) * entry.factor);
        EXPECT_EQ(entry.create(INT64_MIN / entry.factor, &result), CRUX_OK);
        EXPECT_EQ(entry.create(INT64_MIN / entry.factor - 1, &result), CRUX_ERR_OUT_OF_RANGE);
        EXPECT_EQ(result.private_ns, (INT64_MIN / entry.factor) * entry.factor);
        EXPECT_EQ(entry.create(0, nullptr), CRUX_ERR_INVALID_ARGUMENT);
    }
}

TEST(TimeTypes, SignedFloorAndCeilAtEdges) {
    struct Example {
        int64_t nanoseconds;
        int64_t floor_us;
        int64_t ceil_us;
        int64_t floor_ms;
        int64_t ceil_ms;
    };
    constexpr Example cases[] = {
        {0, 0, 0, 0, 0},
        {1, 0, 1, 0, 1},
        {-1, -1, 0, -1, 0},
        {1500, 1, 2, 0, 1},
        {-1500, -2, -1, -1, 0},
    };
    for (const auto &entry : cases) {
        crux_duration_t value = crux_duration_from_ns(entry.nanoseconds);
        EXPECT_EQ(crux_duration_to_ns(value), entry.nanoseconds);
        EXPECT_EQ(crux_duration_to_us_floor(value), entry.floor_us);
        EXPECT_EQ(crux_duration_to_us_ceil(value), entry.ceil_us);
        EXPECT_EQ(crux_duration_to_ms_floor(value), entry.floor_ms);
        EXPECT_EQ(crux_duration_to_ms_ceil(value), entry.ceil_ms);
    }
}

TEST(TimeTypes, SignedFloorAndCeilHandleInt64Extremes) {
    const crux_duration_t max_value = crux_duration_from_ns(INT64_MAX);
    const crux_duration_t min_value = crux_duration_from_ns(INT64_MIN);
    EXPECT_EQ(crux_duration_to_us_floor(max_value), INT64_MAX / 1000);
    EXPECT_EQ(crux_duration_to_us_ceil(max_value), INT64_MAX / 1000 + 1);
    EXPECT_EQ(crux_duration_to_ms_floor(max_value), INT64_MAX / 1000000);
    EXPECT_EQ(crux_duration_to_ms_ceil(max_value), INT64_MAX / 1000000 + 1);
    EXPECT_EQ(crux_duration_to_us_floor(min_value), INT64_MIN / 1000 - 1);
    EXPECT_EQ(crux_duration_to_us_ceil(min_value), INT64_MIN / 1000);
    EXPECT_EQ(crux_duration_to_ms_floor(min_value), INT64_MIN / 1000000 - 1);
    EXPECT_EQ(crux_duration_to_ms_ceil(min_value), INT64_MIN / 1000000);
}

TEST(TimeTypes, MonotonicArithmeticHasCheckedNonnegativeDomain) {
    crux_monotonic_time_t out{88};
    crux_monotonic_time_t base{100};
    EXPECT_EQ(crux_monotonic_add_duration(base, crux_duration_from_ns(-100), &out), CRUX_OK);
    EXPECT_EQ(out.private_ns, 0);
    EXPECT_EQ(crux_monotonic_add_duration(base, crux_duration_from_ns(-101), &out),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(out.private_ns, 0);
    EXPECT_EQ(crux_monotonic_add_duration({INT64_MAX}, crux_duration_from_ns(1), &out),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(crux_monotonic_add_duration({0}, crux_duration_from_ns(INT64_MIN), &out),
              CRUX_ERR_OUT_OF_RANGE);
    EXPECT_EQ(crux_monotonic_add_duration({-1}, crux_duration_from_ns(0), &out),
              CRUX_ERR_INVALID_ARGUMENT);
    EXPECT_EQ(crux_monotonic_add_duration(base, crux_duration_from_ns(0), nullptr),
              CRUX_ERR_INVALID_ARGUMENT);
    crux_duration_t negative_one = crux_duration_from_ns(-1);
    EXPECT_EQ(crux_monotonic_add_duration({INT64_MAX}, negative_one, &out), CRUX_OK);
    EXPECT_EQ(out.private_ns, INT64_MAX - 1);
}

TEST(TimeTypes, MonotonicComparisonsAndSignedDifference) {
    const crux_monotonic_time_t early{0};
    const crux_monotonic_time_t late{INT64_MAX};
    EXPECT_TRUE(crux_monotonic_is_before(early, late));
    EXPECT_FALSE(crux_monotonic_is_before(late, early));
    EXPECT_TRUE(crux_monotonic_is_at_or_before(early, early));
    EXPECT_EQ(crux_monotonic_diff(late, early).private_ns, INT64_MAX);
    EXPECT_EQ(crux_monotonic_diff(early, late).private_ns, -INT64_MAX);
    EXPECT_EQ(crux_monotonic_diff(early, early).private_ns, 0);
}
