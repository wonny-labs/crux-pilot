/** @file
 * @brief Controller service observations, reset diagnostics and watchdog access.
 *
 * Thread context after startup only. No application health or recovery policy
 * is implied by these operations.
 */
#ifndef CRUX_SYSTEM_H
#define CRUX_SYSTEM_H

#include <crux/status.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Boot-local feed authority, zero-invalid; copying does not create a new owner.
 * This token is not a security boundary.
 */
typedef struct {
    uint32_t reserved;
} crux_watchdog_t;

typedef enum {
    CRUX_WATCHDOG_UNAVAILABLE,
    CRUX_WATCHDOG_IDLE,
    CRUX_WATCHDOG_ACTIVE,
    CRUX_WATCHDOG_FAULTED,
    CRUX_WATCHDOG_EXPIRED
} crux_watchdog_state_t;

/** Per-service status; a successful snapshot is not application readiness. */
typedef struct {
    crux_status_t watchdog_status;
    crux_watchdog_state_t watchdog_state;
    bool watchdog_emulated;
    uint32_t watchdog_timeout_ms;
    crux_status_t reset_status;
    crux_status_t reset_cause_status;
} crux_system_status_t;

/** Latched reset flags; supported identifies the flags the provider can report. */
typedef struct {
    uint32_t supported;
    uint32_t causes;
} crux_reset_cause_t;

enum {
    CRUX_RESET_CAUSE_POR = 1u << 0,
    CRUX_RESET_CAUSE_PIN = 1u << 1,
    CRUX_RESET_CAUSE_SOFTWARE = 1u << 2,
    CRUX_RESET_CAUSE_WATCHDOG = 1u << 3,
    CRUX_RESET_CAUSE_DEBUG = 1u << 4,
    CRUX_RESET_CAUSE_CPU_LOCKUP = 1u << 5,
    CRUX_RESET_CAUSE_HARDWARE = 1u << 6,
    CRUX_RESET_CAUSE_SECURITY = 1u << 7,
    CRUX_RESET_CAUSE_USER = 1u << 8,
    CRUX_RESET_CAUSE_TEMPERATURE = 1u << 9,
    CRUX_RESET_CAUSE_OTHER = 1u << 10
};

/** Observe supported services without changing their state.
 * @param[out] out Full snapshot on success; unchanged on failure.
 * @return CRUX_OK or a crux_status_t error. Check individual status fields.
 */
crux_status_t crux_system_status_get(crux_system_status_t *out);

/** Read latched, possibly accumulated reset causes.
 * @param[out] out Flag set on success; unchanged on failure.
 * @return CRUX_OK or a crux_status_t error. Multiple or zero flags are valid.
 */
crux_status_t crux_system_reset_cause_get(crux_reset_cause_t *out);

/** Clear reset evidence after a successful public cause read.
 * @return CRUX_OK if the provider reports cleared flags, or a crux_status_t error.
 * Preserve needed diagnostics before calling; each admitted attempt consumes
 * permission to clear, even on error.
 */
crux_status_t crux_system_reset_cause_clear(void);

/** Request a system reset; a successful target dispatch does not return.
 * @return CRUX_ERR_NOT_SUPPORTED on normal Host; other native failures may hang.
 * Does not flush storage, drain logs or perform application safe-state actions.
 */
crux_status_t crux_system_reset(void);

/** Start the boot's single watchdog activation.
 * @param timeout_ms Requested interval; the supported range and step are provider-specific.
 * @param[out] out Valid feed token on success; zero on failure.
 * @return CRUX_OK or a crux_status_t error. Uncertain native activation cannot be retried.
 */
crux_status_t crux_watchdog_start(uint32_t timeout_ms, crux_watchdog_t *out);

/** Feed the watchdog using the issued authority.
 * @param watchdog Token from successful crux_watchdog_start().
 * @return CRUX_OK means native acceptance, not proof of hardware health or timing.
 */
crux_status_t crux_watchdog_feed(crux_watchdog_t watchdog);

#ifdef __cplusplus
}
#endif

#endif
