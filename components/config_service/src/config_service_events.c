/* The event port of the configuration service, as a thin translation onto the public emit. It holds
 * no state and takes no lock of its own. The name is a key from the table, never a client's text. */
#include "config_service_port.h"

#include <crux/security_event.h>

void crux_config_service_port_event(crux_config_service_event_t event,
                                    crux_access_session_t session, const char *name)
{
    crux_security_event_t record = {.subject = name};
    char user[CRUX_ACCESS_NAME_SIZE];

    /* No default case: the compiler refuses a port event that this table does not map. */
    switch (event) {
    case CRUX_CONFIG_SERVICE_EVENT_UPDATED:
        record.id = CRUX_SECURITY_EVENT_ID_CONFIG_UPDATED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_CONFIG_SERVICE_EVENT_REJECTED:
        record.id = CRUX_SECURITY_EVENT_ID_CONFIG_REJECTED;
        record.result = CRUX_SECURITY_EVENT_INVALID;
        break;
    case CRUX_CONFIG_SERVICE_EVENT_SAVED:
        record.id = CRUX_SECURITY_EVENT_ID_CONFIG_SAVED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_CONFIG_SERVICE_EVENT_SAVE_FAILED:
        record.id = CRUX_SECURITY_EVENT_ID_CONFIG_SAVE_FAILED;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    }
    /* A session that has ended leaves the record with no origin. The change still happened. */
    if (crux_access_session_origin(session, &record.channel, record.source,
                                   &record.source_length) != CRUX_ACCESS_OK) {
        record.channel = 0;
        record.source_length = 0;
    }
    if (crux_access_session_user(session, user, sizeof(user)) == CRUX_ACCESS_OK) {
        record.user = user;
    }
    crux_security_event_emit(&record);
}
