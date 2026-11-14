/* The event port of the file service, as a thin translation onto the public emit (Issue #242). It
 * holds no state and takes no lock of its own. The subject is the area, which comes from the
 * composition's table. A file name is never recorded, because it is client input. */
#include "file_service_port.h"

#include <crux/security_event.h>

void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
{
    crux_security_event_t record = {.subject = area};
    char user[CRUX_ACCESS_NAME_SIZE];

    (void)name;
    (void)new_name;
    /* No default case: the compiler refuses a port event that this table does not map. */
    switch (event) {
    case CRUX_FILE_SERVICE_EVENT_CREATED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_CREATED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_FILE_SERVICE_EVENT_REMOVED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_REMOVED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_FILE_SERVICE_EVENT_RENAMED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_RENAMED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_PARTIAL_REMOVED;
        record.result = CRUX_SECURITY_EVENT_OK;
        break;
    case CRUX_FILE_SERVICE_EVENT_RECOVERY_NEEDED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_RECOVERY_NEEDED;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    case CRUX_FILE_SERVICE_EVENT_NAME_REJECTED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_NAME_REJECTED;
        record.result = CRUX_SECURITY_EVENT_INVALID;
        break;
    case CRUX_FILE_SERVICE_EVENT_COMMIT_FAILED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_COMMIT_FAILED;
        record.result = CRUX_SECURITY_EVENT_FAILED;
        break;
    case CRUX_FILE_SERVICE_EVENT_STORAGE_FAILED:
        record.id = CRUX_SECURITY_EVENT_ID_FILE_STORAGE_FAILED;
        record.result = storage_limit ? CRUX_SECURITY_EVENT_LIMIT : CRUX_SECURITY_EVENT_FAILED;
        break;
    }
    /* What start finds has no session, and an ended session leaves no origin. */
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
