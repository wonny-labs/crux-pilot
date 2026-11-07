/* Private port of the configuration service. A composition supplies it, and no provider exists
 * until Issue #182 lands, so that a product image links only once one is added. */
#ifndef CRUX_CONFIG_SERVICE_PORT_H
#define CRUX_CONFIG_SERVICE_PORT_H

#include <crux/config_service.h>

/* How long a save waits for the lock that another save holds across storage. */
#ifndef CRUX_CONFIG_SERVICE_SAVE_WAIT_MS
#define CRUX_CONFIG_SERVICE_SAVE_WAIT_MS 5000u
#endif

/* The identifiers of Issue #182, 0x0400 to 0x0403. */
typedef enum {
    CRUX_CONFIG_SERVICE_EVENT_UPDATED,
    CRUX_CONFIG_SERVICE_EVENT_REJECTED,
    CRUX_CONFIG_SERVICE_EVENT_SAVED,
    CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED
} crux_config_service_event_t;

/*
 * Report an event. name is a parameter's key from the table, never a client's text, and an event
 * carries no value. It is called with no service lock held, and only for operations that take a
 * session, so that a control thread never meets the event lock. It must not wait on storage or call
 * back into the service.
 */
void crux_config_service_port_event(crux_config_service_event_t event,
                                    crux_access_session_t session, const char *name);

#endif
