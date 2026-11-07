/* The reset port of the reboot service, as a thin translation onto crux_system_reset(). It flushes
 * nothing: the service has given events and saves their bounded time before it calls this. */
#include "reboot_port.h"

#include <crux/system.h>

crux_status_t crux_reboot_port_reset(void)
{
    return crux_system_reset();
}
