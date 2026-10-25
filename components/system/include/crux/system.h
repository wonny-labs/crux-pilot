#ifndef CRUX_SYSTEM_H
#define CRUX_SYSTEM_H
#include <crux/status.h>
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Boot-local, opaque, zero-invalid feed authority; not a security boundary.
 * No stop/rearm/release. A copied token delegates access to the same owner. */
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
typedef struct {
    crux_status_t watchdog_status;
    crux_watchdog_state_t watchdog_state;
    bool watchdog_emulated;
    uint32_t watchdog_timeout_ms;
    crux_status_t reset_status;
    crux_status_t reset_cause_status;
} crux_system_status_t;
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
/* Task context after startup only; private serialization may block.
 * OK means a complete service report, never application health/readiness.
 * Individual service errors remain in their fields. On failure output unchanged.
 * Query never arms/feeds/clears/resets. Timeout is last successful nominal request,
 * zero before success; stale in FAULTED, not a hardware timing guarantee. */
crux_status_t crux_system_status_get(crux_system_status_t *out);
/* Reads latched, possibly accumulated native reasons; multiple/OTHER/zero flags
 * are meaningful, not an ordered last-boot diagnosis. Failed output unchanged.
 * Composition's boot diagnostic owner retains/distributes the initial report. */
crux_status_t crux_system_reset_cause_get(crux_reset_cause_t *out);
/* Absent service returns NOT_SUPPORTED. Otherwise explicit destructive clear
 * requires a successful public cause get;
 * each admitted attempt consumes that permission, even on failure. Verifies
 * reported flags zero. Owner must preserve evidence before clearing; Logging OK
 * is not delivery. No startup clear or component-owned persistent journal. */
crux_status_t crux_system_reset_cause_clear(void);
/* Never returns OK. Target dispatch normally resets and does not return, but
 * native failure may hang instead. Normal Host returns NOT_SUPPORTED without
 * resetting/exiting the machine/process. No drain, persistence or safe-state
 * policy. Real reset requires separate operation approval. */
crux_status_t crux_system_reset(void);
/* Exactly one activation/boot. Output zero on failure; must not hold a live token.
 * Zero timeout OUT_OF_RANGE. Positive bounds/resolution are provider-specific;
 * initial RT1060 and Host emulation:500..64000ms, exact500ms steps (no rounding).
 * Unready can retry; uncertain native installation quarantines for the boot. */
crux_status_t crux_watchdog_start(uint32_t timeout_ms, crux_watchdog_t *out);
/* Only issued token accepted; no automatic health assessment/feed or recovery.
 * Host expiry is observed lazily and cannot be revived. Native feed OK is driver
 * acceptance, not an independent hardware acknowledgement or deadline proof. */
crux_status_t crux_watchdog_feed(crux_watchdog_t watchdog);
#ifdef __cplusplus
}
#endif
#endif
