/**
 * @file
 * Boot-phase decision for the M5 physical Serial/System fixture.
 *
 * A marker in no-init RAM normally survives a watchdog reset but not a power cycle; it is not
 * guaranteed durable (cached external SDRAM, refresh gap during reset). The watchdog is therefore
 * armed only when BOTH hold: the marker is absent or "none", AND the latched reset cause was
 * readable and shows no WATCHDOG reset. A lost marker after a watchdog reset is caught by the
 * reset cause and goes to the read-only report path, so a repeated watchdog activation (a reset
 * loop) needs an unreadable or already cleared cause, and an unreadable cause only observes.
 */
#ifndef M5_PHYSICAL_PHASE_H
#define M5_PHYSICAL_PHASE_H

#include <stdint.h>

#define M5_MARKER_MAGIC UINT32_C(0x4d354454)

enum { M5_MARKER_NONE = 0, M5_MARKER_WD_ARMED = 1, M5_MARKER_DONE = 2 };

typedef enum { M5_PHASE_ARM, M5_PHASE_REPORT, M5_PHASE_OBSERVE } m5_phase_t;

/**
 * valid: nonzero when the marker magic matched. phase: stored marker value.
 * cause_known: nonzero when the reset cause was read successfully before any clear.
 * cause_watchdog: nonzero when that cause includes the WATCHDOG bit.
 */
static inline m5_phase_t m5_decide_phase(int valid, uint32_t phase, int cause_known,
                                         int cause_watchdog)
{
    if (!cause_known)
        return M5_PHASE_OBSERVE; /* never arm blind */
    if (valid && phase == M5_MARKER_DONE)
        return M5_PHASE_OBSERVE;
    if (valid && phase != M5_MARKER_NONE && phase != M5_MARKER_WD_ARMED)
        return M5_PHASE_OBSERVE; /* garbage behind a valid magic */
    if (cause_watchdog || (valid && phase == M5_MARKER_WD_ARMED))
        return M5_PHASE_REPORT;
    return M5_PHASE_ARM;
}

/**
 * Marker to store once an arm attempt finished. A started watchdog is recorded as armed so
 * the next boot only reports; a failed or uncertain start is recorded as done so it is never
 * retried.
 */
static inline uint32_t m5_marker_after_arm(int watchdog_started)
{
    return watchdog_started ? M5_MARKER_WD_ARMED : M5_MARKER_DONE;
}

#endif
