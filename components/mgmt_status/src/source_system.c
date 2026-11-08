/* A thin provider of the System source. It observes only: never arms, feeds or clears. The System
 * contract lets an observation update the watchdog lifecycle, which is why the service reads this
 * at most once per period. */
#include "mgmt_status_port.h"

crux_status_t crux_mgmt_status_port_system(crux_system_status_t *out)
{
    return crux_system_status_get(out);
}
