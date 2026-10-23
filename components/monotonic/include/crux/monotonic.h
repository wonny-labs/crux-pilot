#ifndef CRUX_MONOTONIC_H
#define CRUX_MONOTONIC_H

#include <crux/status.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread context only; ISR calls return NOT_SUPPORTED. No initialisation is needed.
 * Nonnegative, nondecreasing boot/provider-local milliseconds, independent of UTC.
 * Concurrent calls support distinct caller-owned outputs. Overlapping calls may
 * return out of sampling order, and repeated values are permitted.
 * Submillisecond observations round down. Epochs cannot be compared across boots
 * or providers. Resolution, scheduling latency and suspend continuity are not
 * guaranteed. A failed read leaves *out_ms unchanged; NULL is INVALID_ARGUMENT.
 * Unrepresentable readings return OUT_OF_RANGE; native failures return IO.
 */
crux_status_t crux_monotonic_now(int64_t *out_ms);

/* Wait until an observation is >= deadline_ms; past/current deadlines are immediate.
 * Negative input is INVALID_ARGUMENT. A future distance > INT32_MAX milliseconds
 * or an unrepresentable native deadline is OUT_OF_RANGE. Caller checks arithmetic
 * before constructing deadlines. Native failures return IO. This wait is not
 * cancellable and has no hard return-latency bound; retain worker lifetime until
 * successful join. No callback, clock-setting or periodic scheduling policy.
 */
crux_status_t crux_monotonic_sleep_until(int64_t deadline_ms);

#ifdef __cplusplus
}
#endif

#endif
