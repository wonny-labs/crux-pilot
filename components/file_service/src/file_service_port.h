#ifndef CRUX_FILE_SERVICE_PORT_H
#define CRUX_FILE_SERVICE_PORT_H
#include <crux/file_service.h>

#ifndef CRUX_FILE_SERVICE_AREAS_MAX
#define CRUX_FILE_SERVICE_AREAS_MAX 8
#endif
#ifndef CRUX_FILE_SERVICE_TRANSFER_MAX
#define CRUX_FILE_SERVICE_TRANSFER_MAX 3
#endif
#ifndef CRUX_FILE_SERVICE_DIR_MAX
#define CRUX_FILE_SERVICE_DIR_MAX 1
#endif
#ifndef CRUX_FILE_SERVICE_RESERVATIONS
#define CRUX_FILE_SERVICE_RESERVATIONS 8
#endif
#ifndef CRUX_FILE_SERVICE_TEMP_SLOTS
#define CRUX_FILE_SERVICE_TEMP_SLOTS 8
#endif

/* The identifiers of Issue #182, 0x0300 to 0x0307. */
typedef enum {
    CRUX_FILE_SERVICE_EVENT_CREATED,
    CRUX_FILE_SERVICE_EVENT_REMOVED,
    CRUX_FILE_SERVICE_EVENT_RENAMED,
    CRUX_FILE_SERVICE_EVENT_PARTIAL_REMOVED,
    CRUX_FILE_SERVICE_EVENT_RECOVERY_NEEDED,
    CRUX_FILE_SERVICE_EVENT_NAME_REJECTED,
    CRUX_FILE_SERVICE_EVENT_COMMIT_FAILED,
    CRUX_FILE_SERVICE_EVENT_STORAGE_FAILED
} crux_file_service_event_t;

/*
 * Private event port. It never carries a name that a rule refused, only the area. name and new_name
 * are NULL when they do not apply, and the session is all zero for what start finds with no client.
 * storage_limit tells a full medium from another failure for STORAGE_FAILED. The port is called
 * with no service lock held, and it must not wait on storage or call back into the service.
 */
void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit);
#endif
