#include <crux/security_event.h>

#define APP_EVENT_PUMP_STARTED 0x8000

static const crux_security_event_project_id_t project_ids[] = {
    {APP_EVENT_PUMP_STARTED, "app.pump_started", CRUX_SECURITY_EVENT_CHANGE},
};

crux_status_t consumer_start(void)
{
    crux_status_t status = crux_security_event_init(project_ids, 1);
    return status != CRUX_OK ? status : crux_security_event_start();
}

void consumer_record(const char *user)
{
    crux_security_event_t event = {
        .id = APP_EVENT_PUMP_STARTED,
        .result = CRUX_SECURITY_EVENT_OK,
        .channel = 2,
        .source_length = 4,
        .source = {192, 0, 2, 7},
        .user = user,
        .subject = "pump/1",
    };
    crux_security_event_emit(&event);
}

uint32_t consumer_lost(void)
{
    crux_security_event_stats_t stats = {0};
    return crux_security_event_get_stats(&stats) == CRUX_OK ? stats.dropped : 0;
}

const char *consumer_read_name(crux_access_session_t session, uint32_t *cursor)
{
    crux_security_event_record_t record;
    return crux_security_event_read(session, cursor, &record) == CRUX_SECURITY_READ_OK
               ? crux_security_event_name(record.id)
               : NULL;
}
