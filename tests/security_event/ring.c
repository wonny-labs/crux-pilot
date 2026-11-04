/* Ring overflow, the change-class reserve, sequence gaps, loss reports, read cursors and what the
 * drain does with a failing log. Written for any ring and reserve: it also runs against a small
 * build-time ring. */
#include "access_check_stub.h"
#include "event_test.h"

#define RING ((size_t)CRUX_SECURITY_EVENT_RING)
#define RESERVE ((size_t)CRUX_SECURITY_EVENT_RESERVE)
#define ROUTINE_ROOM (RING - RESERVE)

static const crux_access_session_t session = {1};

static crux_security_event_read_result_t read_next(uint32_t *cursor,
                                                   crux_security_event_record_t *record)
{
    return crux_security_event_read(session, cursor, record);
}

/* The sequence number of each retained record, in the order a reader sees them. */
static size_t read_sequences(uint32_t *sequences, size_t capacity)
{
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    size_t count = 0;
    while (count < capacity && read_next(&cursor, &record) == CRUX_SECURITY_READ_OK) {
        sequences[count] = record.sequence;
        count += 1;
    }
    return count;
}

static void reserve_admits_only_change_class(void)
{
    event_test_boot();
    /* Routine events fill everything below the reserve. */
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.emitted == ROUTINE_ROOM);
    CHECK(stats.dropped == 0);
    CHECK(crux_security_event_test_pending() == ROUTINE_ROOM);

    /* In the reserve a routine event is dropped and a change is admitted. */
    event_test_emit_routine();
    stats = event_test_stats();
    CHECK(stats.emitted == ROUTINE_ROOM);
    CHECK(stats.dropped == 1);
    event_test_emit_change();
    stats = event_test_stats();
    CHECK(stats.emitted == ROUTINE_ROOM + 1u);
    CHECK(stats.dropped == 1);
    event_test_emit_routine();
    stats = event_test_stats();
    CHECK(stats.dropped == 2);

    /* The reserve fills with changes. A full ring refuses both. */
    for (size_t index = 1; index < RESERVE; ++index) {
        event_test_emit_change();
    }
    stats = event_test_stats();
    CHECK(stats.emitted == RING);
    CHECK(stats.dropped == 2);
    CHECK(stats.high_water == RING);
    CHECK(crux_security_event_test_pending() == RING);
    event_test_emit_change();
    event_test_emit_routine();
    stats = event_test_stats();
    CHECK(stats.emitted == RING);
    CHECK(stats.dropped == 4);
    CHECK(stats.high_water == RING);
    CHECK(crux_security_event_test_pending() == RING);

    /* A pending record is never overwritten: the oldest is still there, unchanged. */
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED);

    /* Change-class events are all there: none was pushed out by the routine flood. */
    uint32_t sequences[CRUX_SECURITY_EVENT_RING];
    CHECK(read_sequences(sequences, RING) == RING);
    event_test_drain_all();
}

static void admission_follows_pending_not_position(void)
{
    /* After a drain, retained records may be overwritten and routine events have room again. */
    event_test_boot();
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    event_test_emit_routine();
    CHECK(event_test_stats().dropped == 1);
    event_test_drain_all();
    CHECK(crux_security_event_test_pending() == 0);
    event_test_emit_routine();
    CHECK(event_test_stats().dropped == 1);
    CHECK(crux_security_event_test_pending() == 1);
}

static void change_never_loses_to_routine_flood(void)
{
    event_test_boot();
    for (size_t index = 0; index < 3u * RING; ++index) {
        event_test_emit_routine();
    }
    event_test_emit_change();
    CHECK(event_test_stats().emitted == ROUTINE_ROOM + 1u);
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    bool found = false;
    while (read_next(&cursor, &record) == CRUX_SECURITY_READ_OK) {
        found = found || record.id == CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED;
    }
    CHECK(found);
}

static void every_drop_leaves_a_gap(void)
{
    event_test_boot();
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    /* Three drops, each taking a number. */
    event_test_emit_routine();
    event_test_emit_routine();
    event_test_emit_routine();
    event_test_emit_change();
    uint32_t sequences[CRUX_SECURITY_EVENT_RING];
    size_t count = read_sequences(sequences, RING);
    CHECK(count == ROUTINE_ROOM + 1u);
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        CHECK(sequences[index] == index + 1u);
    }
    CHECK(sequences[ROUTINE_ROOM] == ROUTINE_ROOM + 4u);

    /* The drain reports them: one line with the first lost number and the count. */
    event_test_drain_all();
    bool reported = false;
    char expected[64];
    snprintf(expected, sizeof(expected), " first=%zu count=3 res=failed", ROUTINE_ROOM + 1u);
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        const event_test_line_t *line = event_test_line(index);
        if (strncmp(line->text, "evt=event.lost ", 15) == 0) {
            CHECK(!reported);
            reported = true;
            CHECK(strstr(line->text, expected) != NULL);
            CHECK(line->level == CRUX_LOG_WARN);
        }
    }
    CHECK(reported);
    /* Nothing is reported twice. */
    size_t lines = event_test_line_count();
    event_test_drain_all();
    CHECK(event_test_line_count() == lines);
}

static void lost_line_keeps_the_numbering_whole(void)
{
    event_test_boot();
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    event_test_emit_routine(); /* Number ROUTINE_ROOM + 1 is lost. */
    event_test_drain_all();
    /* The lost line takes the next number, so the next record's number follows it. */
    char expected[32];
    snprintf(expected, sizeof(expected), "seq=%zu ", ROUTINE_ROOM + 2u);
    bool found = false;
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        const event_test_line_t *line = event_test_line(index);
        if (strncmp(line->text, "evt=event.lost ", 15) == 0) {
            CHECK(strstr(line->text, expected) != NULL);
            found = true;
        }
    }
    CHECK(found);
    event_test_emit_routine();
    uint32_t cursor = ROUTINE_ROOM + 3u;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == ROUTINE_ROOM + 3u);
    /* It is a line only: no record with its number is retained. */
    cursor = ROUTINE_ROOM + 2u;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == ROUTINE_ROOM + 3u);
}

static void full_ring_reports_loss_with_a_free_slot(void)
{
    event_test_boot();
    /* Nothing is pending and nothing is lost: an idle drain writes nothing. */
    CHECK(crux_security_event_test_drain_step() == 0);

    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    for (size_t index = 0; index < RESERVE; ++index) {
        event_test_emit_change();
    }
    event_test_emit_change(); /* Lost: the ring is full. */
    CHECK(crux_security_event_test_pending() == RING);

    /* The first line frees a slot, and the report can follow it. */
    unsigned lines = crux_security_event_test_drain_step();
    CHECK(lines >= 1);
    CHECK(strncmp(event_test_line(0)->text, "evt=access.login_failed ", 24) == 0);
    event_test_drain_all();
    size_t reports = 0;
    size_t records = 0;
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        if (strncmp(event_test_line(index)->text, "evt=event.lost ", 15) == 0) {
            reports += 1;
        } else {
            records += 1;
        }
    }
    CHECK(reports == 1);
    CHECK(records == RING);
}

static void burst_is_bounded(void)
{
    event_test_boot();
    for (size_t index = 0; index < 3u * RING; ++index) {
        event_test_emit_routine();
    }
    CHECK(crux_security_event_test_drain_step() == CRUX_SECURITY_EVENT_DRAIN_BURST);

    /* One lost line in a burst at most, so a flood of reports cannot starve the records. */
    event_test_boot();
    for (unsigned round = 0; round < 8u; ++round) {
        for (size_t index = 0; index < ROUTINE_ROOM + 1u; ++index) {
            event_test_emit_routine(); /* New loss before every burst. */
        }
        size_t before = event_test_line_count();
        size_t pending = crux_security_event_test_pending();
        CHECK(crux_security_event_test_drain_step() == CRUX_SECURITY_EVENT_DRAIN_BURST);
        size_t reports = 0;
        for (size_t index = before; index < event_test_line_count(); ++index) {
            reports += strncmp(event_test_line(index)->text, "evt=event.lost ", 15) == 0 ? 1u : 0u;
        }
        CHECK(reports == 1);
        CHECK(crux_security_event_test_pending() ==
              pending - (CRUX_SECURITY_EVENT_DRAIN_BURST - 1u));
        event_test_lines_clear();
    }
}

static void lose_more_during_a_write(void *context)
{
    (void)context;
    event_test_emit_routine(); /* Fills the room that the line just freed. */
    event_test_emit_routine(); /* Refused. */
}

/* Even with new loss before every line, a burst has one report, and the rest are records. */
static void report_cannot_starve_records(void)
{
    event_test_boot();
    for (size_t index = 0; index <= ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    event_test_log_hook(lose_more_during_a_write, NULL);
    unsigned written = crux_security_event_test_drain_step();
    event_test_log_hook(NULL, NULL);
    CHECK(written == CRUX_SECURITY_EVENT_DRAIN_BURST);
    size_t reports = 0;
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        reports += strncmp(event_test_line(index)->text, "evt=event.lost ", 15) == 0 ? 1u : 0u;
    }
    CHECK(reports == 1);
}

static void lines_stay_in_order(void)
{
    event_test_boot();
    const size_t count = ROUTINE_ROOM;
    for (size_t index = 0; index < count; ++index) {
        event_test_emit_routine();
    }
    event_test_drain_all();
    CHECK(event_test_line_count() == count);
    for (size_t index = 0; index < count; ++index) {
        char expected[32];
        snprintf(expected, sizeof(expected), " seq=%zu ", index + 1u);
        CHECK(strstr(event_test_line(index)->text, expected) != NULL);
    }
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.delivered == count);
    CHECK(stats.log_errors == 0);
}

static void log_failure_moves_on(void)
{
    event_test_boot();
    event_test_log_status(CRUX_ERR_IO);
    event_test_emit_routine();
    event_test_emit_routine();
    CHECK(event_test_drain_all() == 2);
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.delivered == 0);
    CHECK(stats.log_errors == 2);
    CHECK(stats.emitted == 2);
    CHECK(crux_security_event_test_pending() == 0);
    /* No retry, and the records stay readable. */
    CHECK(event_test_drain_all() == 0);
    uint32_t sequences[2] = {0};
    CHECK(read_sequences(sequences, 2) == 2);
    CHECK(sequences[0] == 1 && sequences[1] == 2);

    /* The log path recovers, and later records flow. */
    event_test_log_status(CRUX_OK);
    event_test_emit_routine();
    CHECK(event_test_drain_all() == 1);
    stats = event_test_stats();
    CHECK(stats.delivered == 1);
    CHECK(stats.log_errors == 2);
}

static void lock_timeout_is_counted_without_a_number(void)
{
    event_test_boot();
    event_test_emit_routine();
    event_test_drain_all();
    event_test_lines_clear();

    event_test_hold_ring_lock();
    event_test_emit_change(); /* Waits 10 ms and gives up. */
    event_test_emit_routine();
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.dropped == 2);
    CHECK(stats.emitted == 1);
    event_test_release_ring_lock();

    /* No number was taken, so no gap exists for them. */
    event_test_emit_routine();
    uint32_t cursor = 2;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 2);

    /* The report has a count and no first number. */
    event_test_drain_all();
    bool found = false;
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        const event_test_line_t *line = event_test_line(index);
        if (strncmp(line->text, "evt=event.lost ", 15) == 0) {
            found = true;
            CHECK(strstr(line->text, "first=") == NULL);
            CHECK(strstr(line->text, " count=2 ") != NULL);
        }
    }
    CHECK(found);

    /* With both kinds, the count covers both and first is the numbered one. */
    event_test_boot();
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    event_test_emit_routine();
    event_test_hold_ring_lock();
    event_test_emit_routine();
    event_test_release_ring_lock();
    event_test_drain_all();
    char expected[64];
    snprintf(expected, sizeof(expected), " first=%zu count=2 ", ROUTINE_ROOM + 1u);
    found = false;
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        const event_test_line_t *line = event_test_line(index);
        if (strncmp(line->text, "evt=event.lost ", 15) == 0) {
            found = true;
            CHECK(strstr(line->text, expected) != NULL);
        }
    }
    CHECK(found);
}

static void lost_only_reports_without_a_pending_record(void)
{
    /* The periodic wake finds a loss with nothing pending and no signal. */
    event_test_boot();
    event_test_hold_ring_lock();
    event_test_emit_routine();
    event_test_release_ring_lock();
    CHECK(crux_security_event_test_pending() == 0);
    CHECK(crux_security_event_test_drain_step() == 1);
    CHECK(strstr(event_test_last_line(), "evt=event.lost seq=1 ") != NULL);
    CHECK(strstr(event_test_last_line(), " count=1 res=failed") != NULL);
}

static void cursor_walks_the_ring(void)
{
    event_test_boot();
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    event_test_record_fill(&record);

    /* An empty ring ends at once, and changes nothing. */
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_END);
    CHECK(cursor == 0);
    CHECK(event_test_record_untouched(&record));

    event_test_emit_routine();
    event_test_emit_change();
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_FAILED);
    CHECK(cursor == 2);
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 2);
    CHECK(record.id == CRUX_SECURITY_EVENT_ID_ACCESS_ACCOUNT_CREATED);
    CHECK(cursor == 3);
    event_test_record_fill(&record);
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_END);
    CHECK(cursor == 3);
    CHECK(event_test_record_untouched(&record));

    /* A later record continues from the same cursor. */
    event_test_emit_routine();
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 3);

    /* A read does not remove anything. */
    uint32_t again = 0;
    CHECK(read_next(&again, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);
}

static void overwritten_records_show_as_a_gap(void)
{
    event_test_boot();
    /* More records than the ring holds, drained as they go, so retained ones are overwritten. */
    const size_t total = RING + 5u;
    for (size_t index = 0; index < total; ++index) {
        event_test_emit_routine();
        event_test_drain_all();
    }
    CHECK(event_test_stats().dropped == 0);

    /* A cursor behind the oldest gets the oldest, with a higher number than it asked for. */
    uint32_t cursor = 1;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == total - RING + 1u);
    CHECK(cursor == record.sequence + 1u);

    /* Exactly the ring's worth stays. */
    uint32_t sequences[CRUX_SECURITY_EVENT_RING];
    CHECK(read_sequences(sequences, RING) == RING);
    for (size_t index = 0; index < RING; ++index) {
        CHECK(sequences[index] == total - RING + 1u + index);
    }
}

static void cursor_from_before_a_reset_restarts(void)
{
    event_test_boot();
    event_test_emit_routine();
    event_test_emit_routine();
    /* The numbering restarted below this cursor, so the caller sees the sequence fall. */
    uint32_t cursor = 1000;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == 1);
    CHECK(cursor == 2);

    /* The cursor that follows the newest record is not ahead: it ends. */
    event_test_emit_routine();
    cursor = 4;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_END);
    CHECK(cursor == 4);
}

static void sequence_wraps_past_zero(void)
{
    event_test_boot();
    crux_security_event_test_set_next_sequence(UINT32_MAX - 1u);
    for (size_t index = 0; index < 4u && index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    uint32_t cursor = UINT32_MAX - 1u;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == UINT32_MAX - 1u);
    CHECK(cursor == UINT32_MAX);
    if (ROUTINE_ROOM >= 4u) {
        CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
        CHECK(record.sequence == UINT32_MAX);
        /* Zero is a cursor's start, so a wrap skips it. */
        CHECK(cursor == 1);
        CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
        CHECK(record.sequence == 1);
        CHECK(cursor == 2);
        CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
        CHECK(record.sequence == 2);
        CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_END);
    }
    /* A cursor of zero starts at the oldest, across the wrap. */
    cursor = 0;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == UINT32_MAX - 1u);

    /* A gap across the wrap: a cursor behind the oldest still finds it. */
    cursor = UINT32_MAX - 5u;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(record.sequence == UINT32_MAX - 1u);
}

static void drops_take_numbers_across_a_wrap(void)
{
    event_test_boot();
    crux_security_event_test_set_next_sequence(UINT32_MAX);
    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    uint32_t before[CRUX_SECURITY_EVENT_RING];
    CHECK(read_sequences(before, RING) == ROUTINE_ROOM);
    CHECK(before[0] == UINT32_MAX);
    if (ROUTINE_ROOM > 1u) {
        CHECK(before[1] == 1);
    }
    event_test_emit_routine(); /* Dropped, and it takes a number. */
    event_test_emit_change();
    uint32_t after[CRUX_SECURITY_EVENT_RING];
    CHECK(read_sequences(after, RING) == ROUTINE_ROOM + 1u);
    CHECK(after[ROUTINE_ROOM] == (uint32_t)ROUTINE_ROOM + 1u);
    event_test_drain_all();
}

static void no_lock_held_across_time_or_log(void)
{
    event_test_boot();
    event_test_log_hook(event_test_expect_ring_lock_free, NULL);
    event_test_wall_hook(event_test_expect_ring_lock_free, NULL);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_MANUAL, true, false);

    for (size_t index = 0; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    event_test_emit_routine(); /* A loss, so the lost line runs too. */
    event_test_drain_all();
    CHECK(event_test_wall_reads() > 0);

    unsigned wall_reads = event_test_wall_reads();
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);
    CHECK(event_test_wall_reads() == wall_reads + 1u);
}

static void read_needs_its_arguments_and_session(void)
{
    event_test_boot();
    event_test_emit_routine();
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    unsigned checks = access_check_stub_count();
    CHECK(crux_security_event_read(session, NULL, &record) ==
          CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT);
    CHECK(crux_security_event_read(session, &cursor, NULL) ==
          CRUX_SECURITY_READ_ERR_INVALID_ARGUMENT);
    CHECK(access_check_stub_count() == checks);

    const struct {
        crux_access_result_t answer;
        crux_security_event_read_result_t expected;
    } answers[] = {
        {CRUX_ACCESS_ERR_DENIED, CRUX_SECURITY_READ_ERR_DENIED},
        {CRUX_ACCESS_ERR_SESSION_INVALID, CRUX_SECURITY_READ_ERR_SESSION_INVALID},
        {CRUX_ACCESS_ERR_NOT_READY, CRUX_SECURITY_READ_ERR_NOT_READY},
        {CRUX_ACCESS_ERR_INVALID_ARGUMENT, CRUX_SECURITY_READ_ERR_NOT_READY},
    };
    event_test_record_fill(&record);
    for (size_t index = 0; index < sizeof(answers) / sizeof(answers[0]); ++index) {
        access_check_stub_set(answers[index].answer);
        CHECK(read_next(&cursor, &record) == answers[index].expected);
        CHECK(cursor == 0);
        CHECK(event_test_record_untouched(&record));
    }
    access_check_stub_set(CRUX_ACCESS_OK);
    CHECK(access_check_stub_asked_for_read());
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_OK);

    /* A ring that cannot answer in time says so, and leaves the output alone. */
    cursor = 0;
    event_test_record_fill(&record);
    event_test_hold_ring_lock();
    CHECK(read_next(&cursor, &record) == CRUX_SECURITY_READ_ERR_NOT_READY);
    event_test_release_ring_lock();
    CHECK(cursor == 0);
    CHECK(event_test_record_untouched(&record));
}

int main(void)
{
    reserve_admits_only_change_class();
    admission_follows_pending_not_position();
    change_never_loses_to_routine_flood();
    every_drop_leaves_a_gap();
    lost_line_keeps_the_numbering_whole();
    full_ring_reports_loss_with_a_free_slot();
    burst_is_bounded();
    report_cannot_starve_records();
    lines_stay_in_order();
    log_failure_moves_on();
    lock_timeout_is_counted_without_a_number();
    lost_only_reports_without_a_pending_record();
    cursor_walks_the_ring();
    overwritten_records_show_as_a_gap();
    cursor_from_before_a_reset_restarts();
    sequence_wraps_past_zero();
    drops_take_numbers_across_a_wrap();
    no_lock_held_across_time_or_log();
    read_needs_its_arguments_and_session();
    return event_test_finish("Security event ring");
}
