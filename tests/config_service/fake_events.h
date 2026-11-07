/* A recording provider of crux_config_service_port_event, for this suite and for the adapters that
 * build on the service. It exists only when Crux builds its own tests. */
#ifndef CONFIG_SERVICE_FAKE_EVENTS_H
#define CONFIG_SERVICE_FAKE_EVENTS_H

#include "config_service_port.h"

typedef struct {
    crux_config_service_event_t event;
    crux_access_session_t session;
    char name[32];
} config_event_record_t;

void config_events_clear(void);
size_t config_events_count(void);
const config_event_record_t *config_events_get(size_t index);
size_t config_events_of(crux_config_service_event_t event);
/* Runs inside every event, until cleared with NULL. */
void config_events_hook(void (*hook)(void *context), void *context);

#endif
