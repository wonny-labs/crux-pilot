/* Private ports of the network settings service. A composition supplies them, and no provider is
 * linked by the core, so that an image links only once a composition chooses one. */
#ifndef CRUX_NETWORK_SETTINGS_PORT_H
#define CRUX_NETWORK_SETTINGS_PORT_H

#include <crux/network_settings.h>

#include <stdbool.h>

/*
 * Apply settings to the primary interface, once, at boot, before any management channel starts.
 * Private native code: it never reaches a public header. Returns false if the interface refused
 * them, and the boot then tries its defaults.
 */
bool crux_network_settings_port_apply(const crux_network_settings_t *settings);

/*
 * The settings now in use on the interface. Returns false while it has no IPv4 address. A port
 * that cannot read the interface returns false, which a client sees as no running values.
 */
bool crux_network_settings_port_running(crux_network_settings_t *settings);

typedef enum {
    CRUX_NETWORK_SETTINGS_EVENT_CHANGED,      /* 0x0800, OK. The whole set is saved. */
    CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED,  /* 0x0801, FAILED. */
    CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED /* 0x0802, OK. The flag is saved as false. */
} crux_network_settings_event_t;

/*
 * Record an event for the session that asked. An event carries no value and no address, and it
 * must not wait on storage or call back into the service. A session that has ended leaves the
 * record with no origin and no user. Called with no service lock held.
 */
void crux_network_settings_port_event(crux_network_settings_event_t event,
                                      crux_access_session_t session);

#endif
