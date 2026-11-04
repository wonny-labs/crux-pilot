#include "interrupt.h"
#include "security_event_internal.h"
#ifdef CRUX_SECURITY_EVENT_TESTING
#include "security_event_test.h"
#endif

#include <crux/time.h>

#include <string.h>

_Static_assert((size_t)CRUX_SECURITY_EVENT_RING <= UINT32_MAX / 2u,
               "a read compares sequence numbers across a wrap");

security_event_state_t crux_security_event_state;

#define STATE crux_security_event_state

static void count_one(atomic_uint_least32_t *counter)
{
    atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}

static bool is_ready(void)
{
    return atomic_load_explicit(&STATE.ready, memory_order_acquire);
}

/* A refusal with no ring lock has no sequence number, so only the next report can show it. */
static void drop_without_a_number(void)
{
    count_one(&STATE.dropped);
    count_one(&STATE.lost_unnumbered);
}

crux_status_t crux_security_event_init(const crux_security_event_project_id_t *project_ids,
                                       size_t count)
{
    if (is_ready()) {
        return CRUX_ERR_INVALID_STATE;
    }
    if (!crux_security_event_project_table_valid(project_ids, count)) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    crux_mutex_t lock;
    crux_status_t status = crux_mutex_create(&lock);
    if (status != CRUX_OK) {
        return status;
    }
    /* One token is enough: the drain handles everything pending each time it wakes. */
    crux_semaphore_t signal;
    status = crux_semaphore_create(0, 1, &signal);
    if (status != CRUX_OK) {
        (void)crux_mutex_destroy(&lock);
        return status;
    }
    crux_security_event_project_table_set(project_ids, count);
    STATE.lock = lock;
    STATE.signal = signal;
    STATE.next_sequence = 1;
    atomic_store_explicit(&STATE.ready, true, memory_order_release);
    return CRUX_OK;
}

static uint32_t sequence_take(void)
{
    uint32_t sequence = STATE.next_sequence;
    STATE.next_sequence = crux_security_event_sequence_next(sequence);
    return sequence;
}

/* The contract's admission table. A record may overwrite a retained one, never a pending one. */
static bool admits(crux_security_event_class_t event_class)
{
    if (STATE.pending >= (size_t)CRUX_SECURITY_EVENT_RING) {
        return false;
    }
    return event_class == CRUX_SECURITY_EVENT_CHANGE ||
           STATE.pending < (size_t)CRUX_SECURITY_EVENT_RING - CRUX_SECURITY_EVENT_RESERVE;
}

static void ring_store(const security_event_stored_t *record)
{
    STATE.slots[STATE.head] = *record;
    STATE.head = (STATE.head + 1u) % (size_t)CRUX_SECURITY_EVENT_RING;
    STATE.pending += 1u;
    if (STATE.retained < (size_t)CRUX_SECURITY_EVENT_RING) {
        STATE.retained += 1u;
    }
    if (STATE.pending > atomic_load_explicit(&STATE.high_water, memory_order_relaxed)) {
        atomic_store_explicit(&STATE.high_water, (uint_least32_t)STATE.pending,
                              memory_order_relaxed);
    }
}

static void loss_note(uint32_t sequence)
{
    if (!STATE.lost_numbered) {
        STATE.lost_numbered = true;
        STATE.lost_first = sequence;
        STATE.lost_count = 0;
    }
    STATE.lost_count += 1u;
}

void crux_security_event_emit(const crux_security_event_t *event)
{
    if (event == NULL || crux_is_in_isr() || !is_ready()) {
        drop_without_a_number();
        return;
    }
    security_event_stored_t record = {0};
    crux_monotonic_time_t now;
    if (crux_monotonic_now(&now) == CRUX_OK) {
        record.uptime_ms = (uint64_t)(now.private_ns / INT64_C(1000000));
    } else {
        record.no_uptime = true;
    }
    unsigned invalid = crux_security_event_fields_copy(event, &record);
    if (invalid != 0) {
        atomic_fetch_add_explicit(&STATE.invalid_fields, invalid, memory_order_relaxed);
    }
    crux_security_event_class_t event_class = crux_security_event_id_class(record.id);

    if (crux_mutex_lock(STATE.lock, CRUX_SECURITY_EVENT_LOCK_MS) != CRUX_OK) {
        drop_without_a_number();
        return;
    }
    record.sequence = sequence_take();
    bool admitted = admits(event_class);
    if (admitted) {
        ring_store(&record);
    } else {
        loss_note(record.sequence);
    }
    (void)crux_mutex_unlock(STATE.lock);

    if (admitted) {
        count_one(&STATE.emitted);
        /* A full semaphore means that the drain is already due to wake. */
        (void)crux_semaphore_give(STATE.signal);
    } else {
        count_one(&STATE.dropped);
    }
}

crux_status_t crux_security_event_get_stats(crux_security_event_stats_t *out)
{
    if (out == NULL) {
        return CRUX_ERR_INVALID_ARGUMENT;
    }
    out->emitted = atomic_load_explicit(&STATE.emitted, memory_order_relaxed);
    out->dropped = atomic_load_explicit(&STATE.dropped, memory_order_relaxed);
    out->delivered = atomic_load_explicit(&STATE.delivered, memory_order_relaxed);
    out->log_errors = atomic_load_explicit(&STATE.log_errors, memory_order_relaxed);
    out->invalid_fields = atomic_load_explicit(&STATE.invalid_fields, memory_order_relaxed);
    out->high_water = atomic_load_explicit(&STATE.high_water, memory_order_relaxed);
    return CRUX_OK;
}

void crux_security_event_clock_fill(uint64_t uptime_ms, bool no_uptime, int64_t *wall_s,
                                    crux_security_event_clock_t *clock)
{
    *wall_s = 0;
    *clock = CRUX_SECURITY_EVENT_CLOCK_NONE;
    crux_monotonic_time_t now;
    crux_realtime_snapshot_t snapshot;
    if (no_uptime || crux_monotonic_now(&now) != CRUX_OK ||
        crux_realtime_get_snapshot(&snapshot) != CRUX_OK || !snapshot.is_valid) {
        return;
    }
    crux_security_event_clock_t source = CRUX_SECURITY_EVENT_CLOCK_NONE;
    switch (snapshot.last_source) {
    case CRUX_REALTIME_SOURCE_UNKNOWN:
        source = CRUX_SECURITY_EVENT_CLOCK_UNKNOWN;
        break;
    case CRUX_REALTIME_SOURCE_RTC:
        source = CRUX_SECURITY_EVENT_CLOCK_RTC;
        break;
    case CRUX_REALTIME_SOURCE_MANUAL:
        source = CRUX_SECURITY_EVENT_CLOCK_MANUAL;
        break;
    case CRUX_REALTIME_SOURCE_EXTERNAL:
        source = CRUX_SECURITY_EVENT_CLOCK_EXTERNAL;
        break;
    case CRUX_REALTIME_SOURCE_NONE:
        break;
    }
    if (source == CRUX_SECURITY_EVENT_CLOCK_NONE) {
        return;
    }
    /* A record cannot be from the future, so a clock that seems to say so gives it no age. */
    uint64_t now_ms = (uint64_t)(now.private_ns / INT64_C(1000000));
    uint64_t age_ms = now_ms > uptime_ms ? now_ms - uptime_ms : 0u;
    /* Time's seconds and a monotonic age in milliseconds are both far from int64_t's limits. */
    int64_t wall_ms = snapshot.timestamp.seconds * INT64_C(1000) +
                      (int64_t)(snapshot.timestamp.nanoseconds / 1000000u) - (int64_t)age_ms;
    if (wall_ms < 0) {
        return;
    }
    *wall_s = wall_ms / INT64_C(1000);
    *clock = source;
}

/* The retained record that follows the cursor, or none. The ring lock is held. */
static const security_event_stored_t *ring_find(uint32_t cursor)
{
    /* Zero starts at the oldest. So does a cursor ahead of the numbering, as one from before a
     * reset is: the caller then sees the sequence fall, rather than an end that lasts a while. */
    bool from_oldest =
        cursor == 0u || crux_security_event_sequence_before(STATE.next_sequence, cursor);
    size_t oldest = (STATE.head + (size_t)CRUX_SECURITY_EVENT_RING - STATE.retained) %
                    (size_t)CRUX_SECURITY_EVENT_RING;
    for (size_t index = 0; index < STATE.retained; ++index) {
        const security_event_stored_t *record =
            &STATE.slots[(oldest + index) % (size_t)CRUX_SECURITY_EVENT_RING];
        if (from_oldest || !crux_security_event_sequence_before(record->sequence, cursor)) {
            return record;
        }
    }
    return NULL;
}

/* Takes the ring lock for the copy only. Static, so that no link can read without the check. */
static crux_security_event_read_result_t ring_read(uint32_t *cursor,
                                                   crux_security_event_record_t *out)
{
    if (crux_mutex_lock(STATE.lock, CRUX_SECURITY_EVENT_LOCK_MS) != CRUX_OK) {
        return CRUX_SECURITY_READ_ERR_NOT_READY;
    }
    const security_event_stored_t *found = ring_find(*cursor);
    if (found == NULL) {
        (void)crux_mutex_unlock(STATE.lock);
        return CRUX_SECURITY_READ_END;
    }
    security_event_stored_t copy = *found;
    (void)crux_mutex_unlock(STATE.lock);
    /* Outside the ring lock, because this takes the Time authority's. */
    int64_t wall_s = 0;
    crux_security_event_clock_t clock = CRUX_SECURITY_EVENT_CLOCK_NONE;
    crux_security_event_clock_fill(copy.uptime_ms, copy.no_uptime, &wall_s, &clock);
    crux_security_event_record_fill(&copy, out);
    out->wall_s = wall_s;
    out->clock = clock;
    *cursor = crux_security_event_sequence_next(copy.sequence);
    return CRUX_SECURITY_READ_OK;
}

crux_security_event_read_result_t crux_security_event_read(crux_access_session_t session,
                                                           uint32_t *cursor,
                                                           crux_security_event_record_t *out)
{
    if (cursor == NULL || out == NULL) {
        return CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT;
    }
    if (!is_ready()) {
        return CRUX_SECURITY_READ_ERR_NOT_READY;
    }
    /* Before the ring lock: a denial's own record needs that lock. */
    switch (crux_access_check(session, CRUX_SECURITY_EVENT_PERMISSION_READ)) {
    case CRUX_ACCESS_OK:
        return ring_read(cursor, out);
    case CRUX_ACCESS_ERR_DENIED:
        return CRUX_SECURITY_READ_ERR_DENIED;
    case CRUX_ACCESS_ERR_SESSION_INVALID:
        return CRUX_SECURITY_READ_ERR_SESSION_INVALID;
    default:
        return CRUX_SECURITY_READ_ERR_NOT_READY;
    }
}

#ifdef CRUX_SECURITY_EVENT_TESTING
void crux_security_event_test_reset(void)
{
    if (atomic_load(&STATE.ready)) {
        (void)crux_mutex_destroy(&STATE.lock);
        (void)crux_semaphore_destroy(&STATE.signal);
    }
    memset(&STATE, 0, sizeof(STATE));
    crux_security_event_project_table_set(NULL, 0);
    crux_security_event_test_drain_clear();
}

crux_mutex_t crux_security_event_test_ring_lock(void)
{
    return STATE.lock;
}

void crux_security_event_test_set_next_sequence(uint32_t sequence)
{
    STATE.next_sequence = sequence;
}

crux_security_event_read_result_t crux_security_event_test_read(uint32_t *cursor,
                                                                crux_security_event_record_t *out)
{
    return ring_read(cursor, out);
}

size_t crux_security_event_test_pending(void)
{
    (void)crux_mutex_lock(STATE.lock, 1000);
    size_t pending = STATE.pending;
    (void)crux_mutex_unlock(STATE.lock);
    return pending;
}
#endif
