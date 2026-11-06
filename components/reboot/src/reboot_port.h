/* Private ports of the reboot service. A composition supplies them, and no provider is linked by
 * the core, so that an image links only once a composition chooses one. */
#ifndef CRUX_REBOOT_PORT_H
#define CRUX_REBOOT_PORT_H

#include <crux/reboot.h>
#include <crux/status.h>

#include <stdbool.h>
#include <stdint.h>

/*
 * Reset the system. Private native code: it never reaches a public header. On a target it does not
 * return, and it returns an error only when the reset could not be made. Whatever it returns is a
 * failure to the service, even CRUX_OK, because a device that is still running did not restart.
 */
crux_status_t crux_reboot_port_reset(void);

/*
 * Sleep for about this long on the worker thread, for the delay before the wait and for each step
 * of the wait. Never called on the server thread.
 */
void crux_reboot_port_sleep(uint32_t milliseconds);

/*
 * Whether security events that were recorded have not yet been handed to Logging. A port that
 * cannot tell returns false. It takes no long lock and does not wait on storage.
 */
bool crux_reboot_port_events_pending(void);

typedef enum {
    CRUX_REBOOT_EVENT_REQUESTED, /* 0x0900, OK. An accepted request. */
    CRUX_REBOOT_EVENT_REFUSED,   /* 0x0901, DENIED. The policy refused, at either check. */
    CRUX_REBOOT_EVENT_FAILED     /* 0x0902, FAILED. The restart did not happen. */
} crux_reboot_event_t;

/*
 * Record an event for the session that asked. An event carries no reason and no subject, and it
 * must not wait on storage or call back into the service. A session that has ended leaves the
 * record with no origin and no user. Called with no service lock held.
 */
void crux_reboot_port_event(crux_reboot_event_t event, crux_access_session_t session);

#endif
