#ifndef CRUX_SECURITY_EVENT_TEST_COMPOSITION
#error "The Security event fake is a test composition and must not be selected for a product image"
#endif

#include "security_event_fake.h"
#include "security_event_internal.h"

#include <pthread.h>
#include <string.h>

static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static bool ready, started;
static uint32_t next_sequence = 1;
static crux_security_event_record_t records[SECURITY_EVENT_FAKE_RECORDS_MAX];
static size_t record_count;
static size_t drops_requested;
static crux_access_result_t access_answer = CRUX_ACCESS_OK;
static uint64_t clock_uptime_ms;
static int64_t clock_wall_s;
static crux_security_event_clock_t clock_source = CRUX_SECURITY_EVENT_CLOCK_NONE;
static crux_security_event_stats_t counts;
static bool stats_are_set;
static crux_security_event_stats_t stats_set;

void security_event_fake_reset(void)
{
    pthread_mutex_lock(&fake_lock);
    ready = started = false;
    next_sequence = 1;
    record_count = 0;
    drops_requested = 0;
    access_answer = CRUX_ACCESS_OK;
    clock_uptime_ms = 0;
    clock_wall_s = 0;
    clock_source = CRUX_SECURITY_EVENT_CLOCK_NONE;
    memset(&counts, 0, sizeof(counts));
    stats_are_set = false;
    crux_security_event_project_table_set(NULL, 0);
    pthread_mutex_unlock(&fake_lock);
}

void security_event_fake_boot(void)
{
    security_event_fake_reset();
    (void)crux_security_event_init(NULL, 0);
}

size_t security_event_fake_count(void)
{
    pthread_mutex_lock(&fake_lock);
    size_t count = record_count;
    pthread_mutex_unlock(&fake_lock);
    return count;
}

const crux_security_event_record_t *security_event_fake_record(size_t index)
{
    return index < security_event_fake_count() ? &records[index] : NULL;
}

size_t security_event_fake_count_of(crux_security_event_id_t id)
{
    size_t count = 0;
    pthread_mutex_lock(&fake_lock);
    for (size_t index = 0; index < record_count; ++index) {
        count += records[index].id == id ? 1u : 0u;
    }
    pthread_mutex_unlock(&fake_lock);
    return count;
}

const crux_security_event_record_t *security_event_fake_last(crux_security_event_id_t id)
{
    const crux_security_event_record_t *found = NULL;
    pthread_mutex_lock(&fake_lock);
    for (size_t index = record_count; index > 0 && found == NULL; --index) {
        if (records[index - 1u].id == id) {
            found = &records[index - 1u];
        }
    }
    pthread_mutex_unlock(&fake_lock);
    return found;
}

void security_event_fake_clear(void)
{
    pthread_mutex_lock(&fake_lock);
    record_count = 0;
    pthread_mutex_unlock(&fake_lock);
}

void security_event_fake_drop_next(size_t count)
{
    pthread_mutex_lock(&fake_lock);
    drops_requested = count;
    pthread_mutex_unlock(&fake_lock);
}

void security_event_fake_access_answer(crux_access_result_t answer)
{
    pthread_mutex_lock(&fake_lock);
    access_answer = answer;
    pthread_mutex_unlock(&fake_lock);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): a test control, in the record's order.
void security_event_fake_clock(uint64_t uptime_ms, int64_t wall_s,
                               crux_security_event_clock_t clock)
{
    pthread_mutex_lock(&fake_lock);
    clock_uptime_ms = uptime_ms;
    clock_wall_s = wall_s;
    clock_source = clock;
    pthread_mutex_unlock(&fake_lock);
}

void security_event_fake_stats_set(const crux_security_event_stats_t *stats)
{
    pthread_mutex_lock(&fake_lock);
    stats_set = *stats;
    stats_are_set = true;
    pthread_mutex_unlock(&fake_lock);
}

crux_status_t crux_security_event_init(const crux_security_event_project_id_t *project_ids,
                                       size_t count)
{
    crux_status_t status = CRUX_OK;
    pthread_mutex_lock(&fake_lock);
    if (ready) {
        status = CRUX_ERR_INVALID_STATE;
    } else if (!crux_security_event_project_table_valid(project_ids, count)) {
        status = CRUX_ERR_INVALID_ARGUMENT;
    } else {
        crux_security_event_project_table_set(project_ids, count);
        ready = true;
    }
    pthread_mutex_unlock(&fake_lock);
    return status;
}

void crux_security_event_emit(const crux_security_event_t *event)
{
    pthread_mutex_lock(&fake_lock);
    if (event == NULL || !ready) {
        counts.dropped += 1u;
        pthread_mutex_unlock(&fake_lock);
        return;
    }
    security_event_stored_t stored = {0};
    counts.invalid_fields += crux_security_event_fields_copy(event, &stored);
    stored.sequence = next_sequence;
    stored.uptime_ms = clock_uptime_ms;
    next_sequence = crux_security_event_sequence_next(next_sequence);
    if (drops_requested > 0u || record_count == SECURITY_EVENT_FAKE_RECORDS_MAX) {
        drops_requested -= drops_requested > 0u ? 1u : 0u;
        counts.dropped += 1u;
    } else {
        crux_security_event_record_t *record = &records[record_count];
        memset(record, 0, sizeof(*record));
        crux_security_event_record_fill(&stored, record);
        record->wall_s = clock_wall_s;
        record->clock = clock_source;
        record_count += 1u;
        counts.emitted += 1u;
    }
    pthread_mutex_unlock(&fake_lock);
}

crux_status_t crux_security_event_start(void)
{
    crux_status_t status = CRUX_OK;
    pthread_mutex_lock(&fake_lock);
    if (!ready) {
        status = CRUX_ERR_NOT_READY;
    } else if (started) {
        status = CRUX_ERR_INVALID_STATE;
    } else {
        started = true;
    }
    pthread_mutex_unlock(&fake_lock);
    return status;
}

crux_status_t crux_security_event_get_stats(crux_security_event_stats_t *out)
{
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&fake_lock);
    *out = stats_are_set ? stats_set : counts;
    pthread_mutex_unlock(&fake_lock);
    return CRUX_OK;
}

/* What the read's checks answer, as the core maps them. The fake's lock is held. */
static crux_security_event_read_result_t read_allowed(void)
{
    if (!ready) {
        return CRUX_SECURITY_READ_ERR_NOT_READY;
    }
    switch (access_answer) {
    case CRUX_ACCESS_OK:
        return CRUX_SECURITY_READ_OK;
    case CRUX_ACCESS_ERR_DENIED:
        return CRUX_SECURITY_READ_ERR_DENIED;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_SECURITY_READ_ERR_SESSION_INVALID;
    default:
        return CRUX_SECURITY_READ_ERR_NOT_READY;
    }
}

crux_security_event_read_result_t crux_security_event_read(crux_access_session_t session,
                                                           uint32_t *cursor,
                                                           crux_security_event_record_t *out)
{
    (void)session;
    if (cursor == NULL || out == NULL) {
        return CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT;
    }
    pthread_mutex_lock(&fake_lock);
    crux_security_event_read_result_t result = read_allowed();
    if (result == CRUX_SECURITY_READ_OK) {
        result = CRUX_SECURITY_READ_END;
        bool from_oldest =
            *cursor == 0u || crux_security_event_sequence_before(next_sequence, *cursor);
        for (size_t index = 0; index < record_count; ++index) {
            if (from_oldest ||
                !crux_security_event_sequence_before(records[index].sequence, *cursor)) {
                *out = records[index];
                *cursor = crux_security_event_sequence_next(records[index].sequence);
                result = CRUX_SECURITY_READ_OK;
                break;
            }
        }
    }
    pthread_mutex_unlock(&fake_lock);
    return result;
}
