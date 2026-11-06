/* The sleep port of the reboot service, on Sync's semaphore. The semaphore is never given, so that
 * taking it with a time limit waits the whole limit. Called on the worker thread only. */
#include "reboot_port.h"

#include <crux/sync.h>

bool crux_reboot_port_sleep(uint32_t milliseconds)
{
    static crux_semaphore_t sleeper;
    static bool created;

    if (!created) {
        /* Created on the first sleep, which is the first request. A failure is a request that
         * fails, and not a delay and a wait that return at once. */
        if (crux_semaphore_create(0u, 1u, &sleeper) != CRUX_OK) {
            return false;
        }
        created = true;
    }
    /* The semaphore is never given, so a take that times out is the sleep. Anything else returned
     * at once and has not slept, and saying that it had would collapse the delay and the wait. */
    return crux_semaphore_take(sleeper, milliseconds) == CRUX_ERR_TIMEOUT;
}
