/* The clock port over the Time contract. It takes no lock that a permission check could wait on, as
 * the port requires. */
#include "access_port.h"

#include <crux/time.h>

/*
 * The port has no way to report a failure, so a clock that failed reads as 0, like one that ran
 * backwards. crux_monotonic_now fails only for a NULL argument, in an ISR or on a provider error.
 *
 * While it reads 0, sessions from before the fault expire, and a bucket with a window stays closed.
 * Two limits follow, and tests/access/clock_fault.c holds them. A login during the fault succeeds,
 * and that session stays valid while the clock is broken. A failure during the fault sets a
 * bucket's last failure to 0 and shrinks its window, so once the clock returns past the quiet
 * period the bucket clears, however recent its real failures were.
 */
uint64_t crux_access_port_now_ms(void)
{
    crux_monotonic_time_t now;
    if (crux_monotonic_now(&now) != CRUX_OK || now.private_ns < 0) {
        return 0;
    }
    return (uint64_t)now.private_ns / UINT64_C(1000000);
}
