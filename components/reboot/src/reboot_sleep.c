/* The sleep port of the reboot service, on Sync's semaphore. The semaphore is never given, so that
 * taking it with a time limit waits the whole limit. Called on the worker thread only. */
#include "reboot_port.h"

#include <crux/sync.h>

void crux_reboot_port_sleep(uint32_t milliseconds)
{
    static crux_semaphore_t sleeper;
    static bool created;

    if (!created) {
        if (crux_semaphore_create(0u, 1u, &sleeper) != CRUX_OK) {
            return;
        }
        created = true;
    }
    (void)crux_semaphore_take(sleeper, milliseconds);
}
