/* The event port of the reboot service, as a thin translation onto the public emit, and the
 * question whether recorded events are still waiting for Logging. It holds no state and takes no
 * lock of its own. An event names no reason, because policy text is the product's. */
#include "reboot_port.h"

#include <crux/security_event.h>

void crux_reboot_port_event(crux_reboot_event_t event, crux_access_session_t session)
{
    crux_security_event_t record = {0};
    char user[CRUX_ACCESS_NAME_SIZE];

    /* No default case: the compiler refuses a port event that this table does not map. */
    switch (event) {
    case CRUX_REBOOT_EVENT_REQUESTED:
        record.id = CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REQUESTED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_REBOOT_EVENT_REFUSED:
        record.id = CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_REFUSED;
        record.result = CRUX_SECURITY_EVENT_DENIED;
        break;
    case CRUX_REBOOT_EVENT_FAILED:
        record.id = CRUX_SECURITY_EVENT_ID_SYSTEM_REBOOT_FAILED;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    }
    /* A session that has ended leaves the record with no origin. The request still happened. */
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

bool crux_reboot_port_events_pending(void)
{
    crux_security_event_stats_t stats;

    if (crux_security_event_get_stats(&stats) != CRUX_OK) {
        return false;
    }
    /* Every admitted record becomes one line, handed to Logging with a result either way, so the
     * records that are still waiting are the admitted ones that have no result yet. The counts are
     * read one by one and wrap, so this is the difference as a signed number, and a report of lost
     * records, which is also a line, can make it read low. It is a bounded wait's hint, not a proof
     * that the ring is empty. */
    return (int32_t)(stats.emitted - stats.delivered - stats.log_errors) > 0;
}
