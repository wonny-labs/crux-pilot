/** @file
 * @brief Portable joinable threads and monotonic sleep.
 *
 * Thread context only. Finite timeout_ms waits use 0 for polling and
 * 1..INT32_MAX for relative monotonic milliseconds. No infinite wait or cancellation.
 */
#ifndef CRUX_THREAD_H
#define CRUX_THREAD_H

#include <crux/status.h>
#include <crux/time_types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zero-invalid value handle. Do not construct or inspect its reserved fields. */
typedef struct {
    uint32_t slot;
    uint32_t generation;
} crux_thread_t;

typedef enum {
    CRUX_THREAD_PRIORITY_LOW = 0,
    CRUX_THREAD_PRIORITY_NORMAL = 1,
    CRUX_THREAD_PRIORITY_HIGH = 2
} crux_thread_priority_t;

/** Thread options. Zero stack size uses the provider default; zero priority is LOW. */
typedef struct {
    size_t stack_size_bytes;
    crux_thread_priority_t priority;
} crux_thread_options_t;

typedef void (*crux_thread_entry_t)(void *argument);

/** Create a joinable thread, which may start before this function returns.
 * @param options Required attributes; set priority explicitly when NORMAL is intended.
 * @param entry Worker function.
 * @param argument Caller-managed data, valid until successful join.
 * @param[out] out Invalid on failure; must not hold a live thread.
 * @return CRUX_OK or a crux_status_t error.
 */
crux_status_t crux_thread_create(const crux_thread_options_t *options, crux_thread_entry_t entry,
                                 void *argument, crux_thread_t *out);

/** Join and retire a completed thread.
 * @param[in,out] thread Invalidated on success; retained on failed or timed-out join.
 * @param timeout_ms Zero polls; positive values wait up to the specified interval.
 * @return CRUX_OK, CRUX_ERR_BUSY, CRUX_ERR_TIMEOUT or another crux_status_t error.
 */
crux_status_t crux_thread_join(crux_thread_t *thread, uint32_t timeout_ms);

/** Sleep for at least the requested duration.
 * @param duration Nonnegative interval; zero returns immediately without yielding.
 * @return CRUX_OK or an argument, context, range or native wait error.
 * Early wakes retry the original deadline. No bound on scheduling latency is promised.
 */
crux_status_t crux_thread_sleep_for(crux_duration_t duration);

/** Sleep until an absolute monotonic deadline in this execution session.
 * @param deadline Nonnegative timestamp; past/current deadlines return immediately.
 * @return CRUX_OK or an argument, context, range or native wait error.
 * Early wakes retry the same deadline; no additional tick padding is applied.
 */
crux_status_t crux_thread_sleep_until(crux_monotonic_time_t deadline);

#ifdef __cplusplus
}
#endif

#endif
