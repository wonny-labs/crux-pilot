/* The fake apply and event ports. They exist only when Crux builds its own tests. */
#include "net_test.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t guard = PTHREAD_MUTEX_INITIALIZER;
enum { APPLIES = 16, EVENTS = 2048 };
static crux_network_settings_t applied[APPLIES];
static size_t apply_count;
static size_t fail_applies;
static bool running_override;
static bool running_has;
static crux_network_settings_t running_value;
static bool last_ok;
static crux_network_settings_t last_applied;
static net_event_t events[EVENTS];
static size_t event_count;

void net_port_reset(void)
{
    pthread_mutex_lock(&guard);
    apply_count = 0;
    fail_applies = 0;
    running_override = false;
    last_ok = false;
    event_count = 0;
    pthread_mutex_unlock(&guard);
}

size_t net_port_apply_count(void)
{
    return apply_count;
}

const crux_network_settings_t *net_port_applied(size_t index)
{
    return index < apply_count ? &applied[index] : NULL;
}

void net_port_fail_applies(size_t count)
{
    fail_applies = count;
}

void net_port_set_running(bool has_running, const crux_network_settings_t *settings)
{
    running_override = true;
    running_has = has_running;
    if (settings != NULL) {
        running_value = *settings;
    }
}

bool crux_network_settings_port_apply(const crux_network_settings_t *settings)
{
    bool ok = true;

    pthread_mutex_lock(&guard);
    if (apply_count < APPLIES) {
        applied[apply_count++] = *settings;
    }
    if (fail_applies > 0) {
        --fail_applies;
        ok = false;
    } else {
        last_ok = true;
        last_applied = *settings;
    }
    pthread_mutex_unlock(&guard);
    return ok;
}

bool crux_network_settings_port_running(crux_network_settings_t *settings)
{
    bool has = false;

    pthread_mutex_lock(&guard);
    if (running_override) {
        has = running_has;
        *settings = running_value;
    } else if (last_ok) {
        has = true;
        *settings = last_applied;
    }
    pthread_mutex_unlock(&guard);
    return has;
}

void net_events_clear(void)
{
    pthread_mutex_lock(&guard);
    event_count = 0;
    pthread_mutex_unlock(&guard);
}

size_t net_events_count(void)
{
    return event_count;
}

const net_event_t *net_events_get(size_t index)
{
    return index < event_count ? &events[index] : NULL;
}

size_t net_events_of(crux_network_settings_event_t event)
{
    size_t found = 0;

    pthread_mutex_lock(&guard);
    for (size_t index = 0; index < event_count; ++index) {
        found += events[index].event == event;
    }
    pthread_mutex_unlock(&guard);
    return found;
}

void crux_network_settings_port_event(crux_network_settings_event_t event,
                                      crux_access_session_t session)
{
    pthread_mutex_lock(&guard);
    if (event_count < EVENTS) {
        events[event_count].event = event;
        events[event_count].session = session;
        ++event_count;
    }
    pthread_mutex_unlock(&guard);
}
