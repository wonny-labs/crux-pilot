#include "security_event_internal.h"
#ifdef CRUX_SECURITY_EVENT_TESTING
#include "security_event_test.h"
#endif

#include <crux/thread.h>
#include <crux/time.h>

#define STATE crux_security_event_state

/* What the drain takes out of the ring in one lock hold. */
typedef struct {
    bool is_lost_report;
    security_event_stored_t record;
    uint32_t lost_sequence;
    bool lost_has_first;
    uint32_t lost_first;
    uint32_t lost_count;
} drain_item_t;

/* The next thing to write. A report takes the next number, in order to keep the numbering whole. It
 * needs a free pending slot, and the caller allows one in a burst, so a flood of reports cannot
 * starve the records. */
static bool drain_take(bool may_report, drain_item_t *item)
{
    if (crux_mutex_lock(STATE.lock, CRUX_SECURITY_EVENT_LOCK_MS) != CRUX_OK) {
        return false;
    }
    bool taken = false;
    uint_least32_t unnumbered = atomic_load_explicit(&STATE.lost_unnumbered, memory_order_relaxed);
    if (may_report && (STATE.lost_numbered || unnumbered != 0u) &&
        STATE.pending < (size_t)CRUX_SECURITY_EVENT_RING) {
        unnumbered = atomic_exchange_explicit(&STATE.lost_unnumbered, 0u, memory_order_relaxed);
        item->is_lost_report = true;
        item->lost_has_first = STATE.lost_numbered;
        item->lost_first = STATE.lost_first;
        item->lost_count = (STATE.lost_numbered ? STATE.lost_count : 0u) + (uint32_t)unnumbered;
        item->lost_sequence = STATE.next_sequence;
        STATE.next_sequence = crux_security_event_sequence_next(STATE.next_sequence);
        STATE.lost_numbered = false;
        taken = true;
    } else if (STATE.pending > 0u) {
        size_t oldest = (STATE.head + (size_t)CRUX_SECURITY_EVENT_RING - STATE.pending) %
                        (size_t)CRUX_SECURITY_EVENT_RING;
        item->is_lost_report = false;
        item->record = STATE.slots[oldest];
        STATE.pending -= 1u;
        taken = true;
    }
    (void)crux_mutex_unlock(STATE.lock);
    return taken;
}

static void write_record(const security_event_stored_t *record)
{
    int64_t wall_s = 0;
    crux_security_event_clock_t clock = CRUX_SECURITY_EVENT_CLOCK_NONE;
    crux_security_event_clock_fill(record->uptime_ms, record->no_uptime, &wall_s, &clock);
    char line[SECURITY_EVENT_LINE_SIZE];
    (void)crux_security_event_line_format(record, wall_s, clock, line, sizeof(line));
    crux_log_level_t level =
        record->result == (uint8_t)CRUX_SECURITY_EVENT_OK ? CRUX_LOG_INFO : CRUX_LOG_WARN;
    /* No retry on an error: a broken log path must not stall the ring. */
    if (crux_log_write(level, SECURITY_EVENT_LOG_TAG, line) == CRUX_OK) {
        atomic_fetch_add_explicit(&STATE.delivered, 1u, memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(&STATE.log_errors, 1u, memory_order_relaxed);
    }
}

static void write_report(const drain_item_t *item)
{
    crux_monotonic_time_t now;
    bool no_uptime = crux_monotonic_now(&now) != CRUX_OK;
    uint64_t uptime_ms = no_uptime ? 0u : (uint64_t)(now.private_ns / INT64_C(1000000));
    int64_t wall_s = 0;
    crux_security_event_clock_t clock = CRUX_SECURITY_EVENT_CLOCK_NONE;
    crux_security_event_clock_fill(uptime_ms, no_uptime, &wall_s, &clock);
    char line[SECURITY_EVENT_LINE_SIZE];
    (void)crux_security_event_lost_line_format(item->lost_sequence, uptime_ms, no_uptime, wall_s,
                                               clock, item->lost_has_first, item->lost_first,
                                               item->lost_count, line, sizeof(line));
    if (crux_log_write(CRUX_LOG_WARN, SECURITY_EVENT_LOG_TAG, line) == CRUX_OK) {
        atomic_fetch_add_explicit(&STATE.delivered, 1u, memory_order_relaxed);
    } else {
        atomic_fetch_add_explicit(&STATE.log_errors, 1u, memory_order_relaxed);
    }
}

unsigned crux_security_event_drain_lines(unsigned limit, bool *may_report)
{
    unsigned lines = 0;
    while (lines < limit) {
        drain_item_t item;
        if (!drain_take(*may_report, &item)) {
            break;
        }
        if (item.is_lost_report) {
            *may_report = false;
            write_report(&item);
        } else {
            write_record(&item.record);
        }
        lines += 1u;
    }
    return lines;
}

static crux_thread_t drain_thread;
static bool drain_started;
#ifdef CRUX_SECURITY_EVENT_TESTING
static atomic_bool drain_stop;
#endif

static bool drain_should_run(void)
{
#ifdef CRUX_SECURITY_EVENT_TESTING
    return !atomic_load(&drain_stop);
#else
    return true;
#endif
}

static void drain_main(void *argument)
{
    (void)argument;
    const crux_duration_t pause = crux_duration_from_ms(CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS);
    /* A burst spans wakes, so that small batches cannot overrun a small log buffer any more than a
     * full ring can. These two say how much of it is used. */
    unsigned written = 0;
    bool may_report = true;
    while (drain_should_run()) {
        crux_status_t waited = crux_semaphore_take(STATE.signal, CRUX_SECURITY_EVENT_DRAIN_WAIT_MS);
        if (waited == CRUX_ERR_TIMEOUT) {
            /* A whole wait with nothing signalled gave the log buffer time to empty. */
            written = 0;
            may_report = true;
        }
        written +=
            crux_security_event_drain_lines(CRUX_SECURITY_EVENT_DRAIN_BURST - written, &may_report);
        if (written >= CRUX_SECURITY_EVENT_DRAIN_BURST) {
            (void)crux_thread_sleep_for(pause);
            written = 0;
            may_report = true;
            /* There may be more with no token left to say so. */
            (void)crux_semaphore_give(STATE.signal);
        } else if (waited != CRUX_OK && waited != CRUX_ERR_TIMEOUT) {
            /* A wait that failed must not become a busy loop. The burst goes on. */
            (void)crux_thread_sleep_for(pause);
        }
    }
}

crux_status_t crux_security_event_start(void)
{
    if (!atomic_load_explicit(&STATE.ready, memory_order_acquire)) {
        return CRUX_ERR_NOT_READY;
    }
    if (drain_started) {
        return CRUX_ERR_INVALID_STATE;
    }
    const crux_thread_options_t options = {CRUX_SECURITY_EVENT_DRAIN_STACK_BYTES,
                                           CRUX_THREAD_PRIORITY_LOW};
    crux_status_t status = crux_thread_create(&options, drain_main, NULL, &drain_thread);
    if (status == CRUX_OK) {
        drain_started = true;
    }
    return status;
}

#ifdef CRUX_SECURITY_EVENT_TESTING
void crux_security_event_test_drain_clear(void)
{
    drain_started = false;
    atomic_store(&drain_stop, false);
}

unsigned crux_security_event_test_drain_step(void)
{
    bool may_report = true;
    return atomic_load(&STATE.ready)
               ? crux_security_event_drain_lines(CRUX_SECURITY_EVENT_DRAIN_BURST, &may_report)
               : 0u;
}

crux_status_t crux_security_event_test_stop_drain(void)
{
    if (!drain_started) {
        return CRUX_ERR_INVALID_STATE;
    }
    atomic_store(&drain_stop, true);
    (void)crux_semaphore_give(STATE.signal);
    crux_status_t status = crux_thread_join(&drain_thread, 30000);
    if (status == CRUX_OK) {
        drain_started = false;
        atomic_store(&drain_stop, false);
    }
    return status;
}
#endif
