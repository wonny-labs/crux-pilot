/* The event port of the network settings service, as a thin translation onto the public emit. It
 * holds no state and takes no lock of its own. An event names no value and no address. */
#include "network_settings_port.h"

#include <crux/security_event.h>

void crux_network_settings_port_event(crux_network_settings_event_t event,
                                      crux_access_session_t session)
{
    crux_security_event_t record = {0};
    char user[CRUX_ACCESS_NAME_SIZE];

    /* No default case: the compiler refuses a port event that this table does not map. */
    switch (event) {
    case CRUX_NETWORK_SETTINGS_EVENT_CHANGED:
        record.id = CRUX_SECURITY_EVENT_ID_NETWORK_CHANGED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED:
        record.id = CRUX_SECURITY_EVENT_ID_NETWORK_SAVE_FAILED;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    case CRUX_NETWORK_SETTINGS_EVENT_SAVED_IGNORED:
        record.id = CRUX_SECURITY_EVENT_ID_NETWORK_SAVED_IGNORED;
        record.result = CRUX_SECURITY_EVENT_OK;
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
