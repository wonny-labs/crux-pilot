/* What a line and a read result state about time, with a controlled clock. An emit samples only the
 * monotonic clock. */
#include "access_check_stub.h"
#include "event_test.h"

static const crux_access_session_t session = {1};

static crux_security_event_record_t read_first(void)
{
    uint32_t cursor = 0;
    crux_security_event_record_t record;
    memset(&record, 0xA5, sizeof(record));
    CHECK(crux_security_event_read(session, &cursor, &record) == CRUX_SECURITY_READ_OK);
    return record;
}

static const char *drain_one_line(void)
{
    event_test_lines_clear();
    CHECK(crux_security_event_test_drain_step() == 1);
    return event_test_last_line();
}

static void clock_unset(void)
{
    event_test_boot();
    event_test_emit_routine();
    crux_security_event_record_t record = read_first();
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    CHECK(record.wall_s == 0);
    CHECK(record.uptime_ms == 1000);
    CHECK(!record.no_uptime);
    const char *line = drain_one_line();
    CHECK(strstr(line, " up=1000 clock=none ") != NULL);
    CHECK(strstr(line, "wall=") == NULL);
}

static void each_source(void)
{
    const struct {
        crux_realtime_source_t source;
        crux_security_event_clock_t clock;
        const char *text;
    } sources[] = {
        {CRUX_REALTIME_SOURCE_RTC, CRUX_SECURITY_EVENT_CLOCK_RTC, "rtc"},
        {CRUX_REALTIME_SOURCE_MANUAL, CRUX_SECURITY_EVENT_CLOCK_MANUAL, "man"},
        {CRUX_REALTIME_SOURCE_EXTERNAL, CRUX_SECURITY_EVENT_CLOCK_EXTERNAL, "ext"},
        {CRUX_REALTIME_SOURCE_UNKNOWN, CRUX_SECURITY_EVENT_CLOCK_UNKNOWN, "unk"},
    };
    for (size_t index = 0; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        event_test_boot();
        event_test_wall_set(1793750400, 0, sources[index].source, true, false);
        event_test_emit_routine();
        crux_security_event_record_t record = read_first();
        CHECK(record.clock == sources[index].clock);
        CHECK(record.wall_s == 1793750400);
        char expected[64];
        snprintf(expected, sizeof(expected), " wall=1793750400 clock=%s ", sources[index].text);
        CHECK(strstr(drain_one_line(), expected) != NULL);
    }

    /* A valid clock with no source makes no claim. */
    event_test_boot();
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_NONE, true, false);
    event_test_emit_routine();
    crux_security_event_record_t record = read_first();
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    CHECK(record.wall_s == 0);
    CHECK(strstr(drain_one_line(), " clock=none ") != NULL);
}

static void wall_is_the_clock_less_the_age(void)
{
    event_test_boot();
    event_test_clock_set_ms(10000);
    event_test_emit_routine();
    /* Five seconds pass before the drain. */
    event_test_clock_set_ms(15000);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_record_t record = read_first();
    CHECK(record.uptime_ms == 10000);
    CHECK(record.wall_s == 1793750395);
    CHECK(strstr(drain_one_line(), " up=10000 wall=1793750395 clock=rtc ") != NULL);

    /* A fraction of a second rounds down, using the snapshot's fraction. */
    event_test_boot();
    event_test_clock_set_ms(10000);
    event_test_emit_routine();
    event_test_clock_set_ms(10300);
    event_test_wall_set(1793750400, 100000000u, CRUX_REALTIME_SOURCE_RTC, true, false);
    record = read_first();
    CHECK(record.wall_s == 1793750399);
    event_test_wall_set(1793750400, 400000000u, CRUX_REALTIME_SOURCE_RTC, true, false);
    record = read_first();
    CHECK(record.wall_s == 1793750400);

    /* A read later than the drain computes its own age. */
    event_test_clock_set_ms(20300);
    event_test_wall_set(1793750500, 0, CRUX_REALTIME_SOURCE_MANUAL, true, false);
    record = read_first();
    CHECK(record.wall_s == 1793750500 - 10 - 1);
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_MANUAL);
}

static void snapshot_failure(void)
{
    const crux_status_t errors[] = {CRUX_ERR_NOT_READY, CRUX_ERR_OUT_OF_RANGE, CRUX_ERR_IO,
                                    CRUX_ERR_NOT_SUPPORTED};
    for (size_t index = 0; index < sizeof(errors) / sizeof(errors[0]); ++index) {
        event_test_boot();
        event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
        event_test_wall_fail(errors[index]);
        event_test_emit_routine();
        crux_security_event_record_t record = read_first();
        CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
        CHECK(record.wall_s == 0);
        /* The line is still written. */
        const char *line = drain_one_line();
        CHECK(strstr(line, " clock=none ") != NULL);
        CHECK(strstr(line, "wall=") == NULL);
        CHECK(strstr(line, " up=1000 ") != NULL);
    }
}

static void no_uptime(void)
{
    event_test_boot();
    event_test_clock_fail(CRUX_ERR_IO);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    event_test_emit_routine();
    event_test_clock_fail(CRUX_OK);
    /* Admitted, but with no age known, so with no wall time at all. */
    CHECK(event_test_stats().emitted == 1);
    crux_security_event_record_t record = read_first();
    CHECK(record.no_uptime);
    CHECK(record.uptime_ms == 0);
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    CHECK(record.wall_s == 0);
    const char *line = drain_one_line();
    CHECK(strstr(line, "up=") == NULL);
    CHECK(strstr(line, "wall=") == NULL);
    CHECK(strstr(line, " clock=none ") != NULL);
}

static void clock_failure_after_the_emit(void)
{
    event_test_boot();
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    event_test_emit_routine();
    /* The record has an uptime, but its age cannot be known now. */
    event_test_clock_fail(CRUX_ERR_IO);
    crux_security_event_record_t record = read_first();
    CHECK(!record.no_uptime);
    CHECK(record.uptime_ms == 1000);
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    const char *line = drain_one_line();
    CHECK(strstr(line, " up=1000 clock=none ") != NULL);
}

static void set_between_emit_and_drain(void)
{
    /* The clock's state at drain is what the line prints. */
    event_test_boot();
    event_test_clock_set_ms(5000);
    event_test_emit_routine();
    event_test_clock_set_ms(7000);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_MANUAL, true, false);
    crux_security_event_record_t record = read_first();
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_MANUAL);
    CHECK(record.wall_s == 1793750398);
    CHECK(strstr(drain_one_line(), " up=5000 wall=1793750398 clock=man ") != NULL);

    /* And a clock invalidated before the drain states none, though it was set at emit. */
    event_test_boot();
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    event_test_emit_routine();
    event_test_wall_set(0, 0, CRUX_REALTIME_SOURCE_RTC, false, false);
    record = read_first();
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    CHECK(strstr(drain_one_line(), " clock=none ") != NULL);
}

static void synchronized_is_not_printed(void)
{
    event_test_boot();
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_EXTERNAL, true, true);
    event_test_emit_routine();
    const char *line = drain_one_line();
    CHECK(strstr(line, " clock=ext ") != NULL);
    CHECK(strstr(line, "sync") == NULL);
}

static void wall_before_the_epoch_makes_no_claim(void)
{
    /* An age longer than the clock's value gives no line a negative time. */
    event_test_boot();
    event_test_clock_set_ms(0);
    event_test_emit_routine();
    event_test_clock_set_ms(UINT64_C(4000000000000));
    event_test_wall_set(1000000000, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_record_t record = read_first();
    CHECK(record.clock == CRUX_SECURITY_EVENT_CLOCK_NONE);
    CHECK(record.wall_s == 0);
    CHECK(strstr(drain_one_line(), "wall=") == NULL);
}

static void clock_running_backwards_is_age_zero(void)
{
    event_test_boot();
    event_test_clock_set_ms(9000);
    event_test_emit_routine();
    event_test_clock_set_ms(1000);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_record_t record = read_first();
    CHECK(record.wall_s == 1793750400);
}

static void emit_takes_no_authority_lock(void)
{
    event_test_boot();
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    unsigned clock_reads = event_test_clock_reads();
    event_test_emit_routine();
    event_test_emit_change();
    /* Only the monotonic clock is sampled, once for each. */
    CHECK(event_test_wall_reads() == 0);
    CHECK(event_test_clock_reads() == clock_reads + 2u);
    /* A dropped event neither reads the authority. */
    for (size_t index = 0; index < 3u * (size_t)CRUX_SECURITY_EVENT_RING; ++index) {
        event_test_emit_routine();
    }
    CHECK(event_test_wall_reads() == 0);
    event_test_drain_all();
    CHECK(event_test_wall_reads() > 0);
}

/* The lost line states time as any other line does, and counts as any other line does. */
static void lost_line_time_and_counts(void)
{
    /* A loss with nothing pending, so the step writes only the report. */
    event_test_boot();
    event_test_clock_set_ms(5000);
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_emit(NULL);
    const char *line = drain_one_line();
    CHECK(strcmp(line,
                 "evt=event.lost seq=1 up=5000 wall=1793750400 clock=rtc count=1 res=failed") == 0);
    CHECK(event_test_line(0)->level == CRUX_LOG_WARN);

    /* The clock unset. */
    event_test_boot();
    crux_security_event_emit(NULL);
    CHECK(strcmp(drain_one_line(), "evt=event.lost seq=1 up=1000 clock=none count=1 res=failed") ==
          0);

    /* The monotonic clock failing at the drain: no uptime and no wall time. */
    event_test_boot();
    event_test_wall_set(1793750400, 0, CRUX_REALTIME_SOURCE_RTC, true, false);
    crux_security_event_emit(NULL);
    event_test_clock_fail(CRUX_ERR_IO);
    CHECK(strcmp(drain_one_line(), "evt=event.lost seq=1 clock=none count=1 res=failed") == 0);
    event_test_clock_fail(CRUX_OK);

    /* The snapshot failing at the drain. */
    event_test_boot();
    crux_security_event_emit(NULL);
    event_test_wall_fail(CRUX_ERR_IO);
    CHECK(strcmp(drain_one_line(), "evt=event.lost seq=1 up=1000 clock=none count=1 res=failed") ==
          0);
    event_test_wall_fail(CRUX_OK);

    /* A report that Logging refuses is an error, and is not sent again. */
    event_test_boot();
    crux_security_event_emit(NULL);
    event_test_log_status(CRUX_ERR_IO);
    CHECK(crux_security_event_test_drain_step() == 1);
    crux_security_event_stats_t stats = event_test_stats();
    CHECK(stats.log_errors == 1);
    CHECK(stats.delivered == 0);
    CHECK(crux_security_event_test_drain_step() == 0);

    /* One that it accepts is delivered. */
    event_test_log_status(CRUX_OK);
    crux_security_event_emit(NULL);
    CHECK(crux_security_event_test_drain_step() == 1);
    stats = event_test_stats();
    CHECK(stats.log_errors == 1);
    CHECK(stats.delivered == 1);
}

int main(void)
{
    clock_unset();
    each_source();
    wall_is_the_clock_less_the_age();
    snapshot_failure();
    no_uptime();
    clock_failure_after_the_emit();
    set_between_emit_and_drain();
    synchronized_is_not_printed();
    wall_before_the_epoch_makes_no_claim();
    clock_running_backwards_is_age_zero();
    emit_takes_no_authority_lock();
    lost_line_time_and_counts();
    return event_test_finish("Security event time");
}
