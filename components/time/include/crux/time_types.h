/**
 * @file
 * @brief Portable Crux time values and checked arithmetic.
 *
 * Nanoseconds are the representation unit, not a clock resolution guarantee.
 * Duration may be negative. Monotonic timestamps are nonnegative and are
 * comparable only within the same provider and boot/execution session.
 */
#ifndef CRUX_TIME_TYPES_H
#define CRUX_TIME_TYPES_H

#include <crux/status.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Storage is visible for by-value use, but private_ns is not a public
 * field-access contract. Application consumers must use the Time APIs.
 * Trusted Crux implementations and tests may access it where necessary.
 */
typedef struct {
    int64_t private_ns;
} crux_duration_t;

typedef struct {
    int64_t private_ns;
} crux_monotonic_time_t;

/* POSIX/Unix seconds since 1970-01-01T00:00:00Z, excluding leap seconds.
 * nanoseconds is a normalized fractional second in [0, 999999999].
 */
typedef struct {
    int64_t seconds;
    uint32_t nanoseconds;
} crux_realtime_t;

/* Only for integer constant expressions with representable results.
 * Casts do not diagnose floating-point, unsigned-wrap or out-of-range inputs.
 */
#define CRUX_DURATION_NS_INIT(ns) {(int64_t)(ns)}
#define CRUX_DURATION_US_INIT(us) {(int64_t)(us) * INT64_C(1000)}
#define CRUX_DURATION_MS_INIT(ms) {(int64_t)(ms) * INT64_C(1000000)}
#define CRUX_DURATION_SEC_INIT(seconds) {(int64_t)(seconds) * INT64_C(1000000000)}

/* These signed 32-bit input ranges are safe to multiply into int64_t ns. */
static inline crux_duration_t crux_duration_from_ns(int64_t ns)
{
    crux_duration_t result = {ns};
    return result;
}

static inline crux_duration_t crux_duration_from_us(int32_t us)
{
    crux_duration_t result = {(int64_t)us * INT64_C(1000)};
    return result;
}

static inline crux_duration_t crux_duration_from_ms(int32_t ms)
{
    crux_duration_t result = {(int64_t)ms * INT64_C(1000000)};
    return result;
}

static inline crux_duration_t crux_duration_from_sec(int32_t seconds)
{
    crux_duration_t result = {(int64_t)seconds * INT64_C(1000000000)};
    return result;
}

/**
 * Construct a signed Duration from checked microseconds.
 * @param us Signed microseconds; negative durations are valid.
 * @param[out] out Caller-owned result, preserved on every failure.
 * @return OK, INVALID_ARGUMENT for NULL, OUT_OF_RANGE on scaling overflow.
 */
crux_status_t crux_duration_try_from_us(int64_t us, crux_duration_t *out);
/**
 * Construct a signed Duration from checked milliseconds.
 * @param ms Signed milliseconds; negative durations are valid.
 * @param[out] out Caller-owned result, preserved on every failure.
 * @return OK, INVALID_ARGUMENT for NULL, OUT_OF_RANGE on scaling overflow.
 */
crux_status_t crux_duration_try_from_ms(int64_t ms, crux_duration_t *out);
/**
 * Construct a signed Duration from checked seconds.
 * @param seconds Signed seconds; negative durations are valid.
 * @param[out] out Caller-owned result, preserved on every failure.
 * @return OK, INVALID_ARGUMENT for NULL, OUT_OF_RANGE on scaling overflow.
 */
crux_status_t crux_duration_try_from_sec(int64_t seconds, crux_duration_t *out);

static inline int64_t crux_duration_to_ns(crux_duration_t duration)
{
    return duration.private_ns;
}

/* Signed mathematical floor and ceil; valid over the full int64_t ns range. */
static inline int64_t crux_duration_to_us_floor(crux_duration_t duration)
{
    int64_t q = duration.private_ns / INT64_C(1000);
    int64_t r = duration.private_ns % INT64_C(1000);
    return q - (r < 0 ? 1 : 0);
}

static inline int64_t crux_duration_to_us_ceil(crux_duration_t duration)
{
    int64_t q = duration.private_ns / INT64_C(1000);
    int64_t r = duration.private_ns % INT64_C(1000);
    return q + (r > 0 ? 1 : 0);
}

static inline int64_t crux_duration_to_ms_floor(crux_duration_t duration)
{
    int64_t q = duration.private_ns / INT64_C(1000000);
    int64_t r = duration.private_ns % INT64_C(1000000);
    return q - (r < 0 ? 1 : 0);
}

static inline int64_t crux_duration_to_ms_ceil(crux_duration_t duration)
{
    int64_t q = duration.private_ns / INT64_C(1000000);
    int64_t r = duration.private_ns % INT64_C(1000000);
    return q + (r > 0 ? 1 : 0);
}

/* Operands must be monotonic samples from one provider and execution session.
 * Invalid manually constructed negative timestamps violate the API contract.
 */
static inline bool crux_monotonic_is_before(crux_monotonic_time_t a, crux_monotonic_time_t b)
{
    return a.private_ns < b.private_ns;
}

static inline bool crux_monotonic_is_at_or_before(crux_monotonic_time_t a, crux_monotonic_time_t b)
{
    return a.private_ns <= b.private_ns;
}

/**
 * Add a signed Duration to a nonnegative Monotonic timestamp.
 * A negative base is INVALID_ARGUMENT. An unrepresentable or negative result
 * is OUT_OF_RANGE. On failure the output remains untouched.
 * @param base Nonnegative sample from the same provider/session as consumers.
 * @param delta Signed interval; no deadline quantization compensation.
 * @param[out] out Assigned only on success; NULL is INVALID_ARGUMENT.
 * @return OK, INVALID_ARGUMENT or OUT_OF_RANGE as specified above.
 */
crux_status_t crux_monotonic_add(crux_monotonic_time_t base, crux_duration_t delta,
                                 crux_monotonic_time_t *out);

/* Signed a - b; valid nonnegative int64_t timestamps never overflow here. */
crux_duration_t crux_monotonic_diff(crux_monotonic_time_t a, crux_monotonic_time_t b);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_TIME_TYPES_H */
