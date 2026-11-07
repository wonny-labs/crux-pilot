#include "fake_events.h"

#include <pthread.h>
#include <string.h>

/* A real port takes calls from several threads, so this one does too. */
static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
enum { RECORDS = 64 };
static config_event_record_t records[RECORDS];
static size_t count;
static void (*event_hook)(void *context);
static void *event_hook_context;

void config_events_clear(void)
{
    pthread_mutex_lock(&guard);
    count = 0;
    pthread_mutex_unlock(&guard);
}

size_t config_events_count(void)
{
    pthread_mutex_lock(&guard);
    size_t found = count;
    pthread_mutex_unlock(&guard);
    return found;
}

const config_event_record_t *config_events_get(size_t index)
{
    return index < count ? &records[index] : NULL;
}

size_t config_events_of(crux_config_service_event_t event)
{
    size_t found = 0;
    pthread_mutex_lock(&guard);
    for (size_t i = 0; i < count; ++i) {
        found += records[i].event == event;
    }
    pthread_mutex_unlock(&guard);
    return found;
}

void config_events_hook(void (*hook)(void *context), void *context)
{
    event_hook = hook;
    event_hook_context = context;
}

void crux_config_service_port_event(crux_config_service_event_t event,
                                    crux_access_session_t session, const char *name)
{
    pthread_mutex_lock(&guard);
    if (count < RECORDS) {
        config_event_record_t *record = &records[count++];
        record->event = event;
        record->session = session;
        strncpy(record->name, name, sizeof(record->name) - 1);
        record->name[sizeof(record->name) - 1] = '\0';
    }
    pthread_mutex_unlock(&guard);
    /* The hook runs outside the guard, because it may cause another event. */
    if (event_hook != NULL) {
        event_hook(event_hook_context);
    }
}
