/* The clock port over the Time contract. It takes no lock that a permission check could wait on, as
 * the port requires. */
#include "access_port.h"

#include <crux/time.h>

/*
 * A clock that failed reads as one that ran backwards, which expires sessions and never clears a
 * delay bucket. Holding the last reading instead would keep a session alive for as long as the
 * clock stays broken.
 */
uint64_t crux_access_port_now_ms(void)
{
    crux_monotonic_time_t now;
    if (crux_monotonic_now(&now) != CRUX_OK || now.private_ns < 0) {
        return 0;
    }
    return (uint64_t)now.private_ns / UINT64_C(1000000);
}
