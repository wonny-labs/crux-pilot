/** @file
 * @brief Bounded CPU busy-wait using the selected native timing provider.
 *
 * No Crux-owned locks, dynamic allocation or scheduler-based blocking. Brief
 * native interrupt-masked clock-read sections are permitted, but the provider
 * must not mask interrupts for the requested delay. ISR support is specific to
 * validated provider/target configurations; no general lock-free guarantee or
 * maximum execution latency. Preemption can extend a wait. No realtime init or
 * RTC is required. Early boot/stopped-clock power states are not qualified.
 * Caller interrupt state is preserved; IRQ-delivery checks assume ordinary
 * thread entry with interrupts enabled, not caller-held interrupt masking.
 */
#ifndef CRUX_DELAY_H
#define CRUX_DELAY_H

#include <crux/status.h>
#include <crux/time_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Query the provider's current supported nonnegative input limit.
 * This observation does not reserve clock headroom; each wait rechecks limits.
 * @param[out] out Assigned only on success; callers own distinct output storage.
 * @return OK, INVALID_ARGUMENT for NULL, NOT_SUPPORTED for unsupported timing
 * configurations, OUT_OF_RANGE for clock exhaustion, or IO for native failure.
 */
crux_status_t crux_delay_get_max_duration(crux_duration_t *out);

/** Consume CPU while scheduled until the requested native interval has elapsed.
 * Zero succeeds immediately without native access or yielding. Negative input
 * is INVALID_ARGUMENT; exceeding the current limit is OUT_OF_RANGE before wait.
 * Native units round upward. No cancellation or maximum return latency.
 * @param duration Requested interval in nanoseconds; representation is not an
 * accuracy guarantee. Caller must use a validated execution context/provider.
 * @return OK, INVALID_ARGUMENT, OUT_OF_RANGE, NOT_SUPPORTED or native IO error.
 */
crux_status_t crux_delay_busy_wait(crux_duration_t duration);

#ifdef __cplusplus
}
#endif
#endif
