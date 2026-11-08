/* The drain's thread: what wakes it, how it paces lines, and how long it waits. This core's timed
 * wake never fires in a run, so a line that the signal does not reach simply never appears. */
#include "access_check_stub.h"
#include "event_test.h"

#include <time.h>

#define BURST ((unsigned)CRUX_SECURITY_EVENT_DRAIN_BURST)
#define ROUTINE_ROOM ((size_t)CRUX_SECURITY_EVENT_RING - CRUX_SECURITY_EVENT_RESERVE)
#define WAIT_LIMIT_MS 10000u

_Static_assert(CRUX_SECURITY_EVENT_DRAIN_BURST >= 4, "the cases below use part of a burst");

/* Not crux_thread_sleep_for, which counts as a pause of the drain in this composition. */
static void rest_1_ms(void)
{
    const struct timespec rest = {0, 1000000};
    (void)nanosleep(&rest, NULL);
}

/* Waits for at least this many lines and pauses. The drain may still be finishing that wake. */
static void wait_for(unsigned lines, unsigned pauses)
{
    for (unsigned waited = 0; waited < WAIT_LIMIT_MS; ++waited) {
        if (event_test_line_count() >= lines && event_test_pauses() >= pauses) {
            break;
        }
        rest_1_ms();
    }
    CHECK(event_test_line_count() >= lines);
    CHECK(event_test_pauses() >= pauses);
}

/* Waits until the drain stays in its wait, so that the next emit starts a new wake. */
static void wait_for_idle(void)
{
    unsigned quiet = 0;
    for (unsigned waited = 0; waited < WAIT_LIMIT_MS && quiet < 3u; ++waited) {
        quiet = event_test_signal_waiting() ? quiet + 1u : 0u;
        rest_1_ms();
    }
    CHECK(quiet >= 3u);
}

static size_t report_lines(void)
{
    size_t reports = 0;
    for (size_t index = 0; index < event_test_line_count(); ++index) {
        reports += strncmp(event_test_line(index)->text, "evt=event.lost ", 15) == 0 ? 1u : 0u;
    }
    return reports;
}

static void start_drain(void)
{
    event_test_boot();
    event_test_clock_real(true);
    CHECK(crux_security_event_start() == CRUX_OK);
}

static void stop_drain(void)
{
    CHECK(crux_security_event_test_stop_drain() == CRUX_OK);
}

static void emit_routine_lines(unsigned count)
{
    for (unsigned index = 0; index < count; ++index) {
        event_test_emit_routine();
    }
}

/* An emit signals only for what it admitted. */
static void signal_on_admission(void)
{
    event_test_reset();
    event_test_emit_routine();
    CHECK(event_test_signal_gives() == 0);

    event_test_boot();
    event_test_emit_routine();
    CHECK(event_test_signal_gives() == 1);
    for (size_t index = 1; index < ROUTINE_ROOM; ++index) {
        event_test_emit_routine();
    }
    CHECK(event_test_signal_gives() == ROUTINE_ROOM);
    event_test_emit_routine(); /* In the reserve, so it is dropped. */
    CHECK(event_test_signal_gives() == ROUTINE_ROOM);
    event_test_emit_change();
    CHECK(event_test_signal_gives() == ROUTINE_ROOM + 1u);
}

/* The signal alone wakes the drain, whose timed wake is a minute away here. */
static void signal_wakes_the_drain(void)
{
    start_drain();
    event_test_emit_routine();
    wait_for(1, 0);
    CHECK(event_test_line_count() == 1);
    stop_drain();
}

/* The wait is finite and uses the setting. */
static void wait_is_bounded(void)
{
    start_drain();
    for (unsigned waited = 0; waited < WAIT_LIMIT_MS && event_test_signal_takes() == 0; ++waited) {
        rest_1_ms();
    }
    CHECK(event_test_signal_last_wait_ms() == CRUX_SECURITY_EVENT_DRAIN_WAIT_MS);
    CHECK(CRUX_SECURITY_EVENT_DRAIN_WAIT_MS > 0);
    stop_drain();
}

/* A full burst signals again, because records can wait with no token left to say so. */
static void signal_again_after_a_burst(void)
{
    event_test_boot();
    event_test_clock_real(true);
    emit_routine_lines(2u * BURST + 1u);
    const unsigned gives = event_test_signal_gives();
    CHECK(crux_security_event_start() == CRUX_OK);
    wait_for(2u * BURST + 1u, 2);
    /* Stopping signals too, so this is read first. */
    for (unsigned waited = 0; waited < WAIT_LIMIT_MS && event_test_signal_gives() < gives + 2u;
         ++waited) {
        rest_1_ms();
    }
    CHECK(event_test_signal_gives() >= gives + 2u);
    CHECK(event_test_line_count() == 2u * BURST + 1u);
    stop_drain();
}

/* Lines count across wakes, so small batches get the same pause as a full burst. */
static void pause_counts_across_wakes(void)
{
    start_drain();
    const unsigned batch = BURST - 1u;
    const unsigned batches = BURST;
    for (unsigned index = 1; index <= batches; ++index) {
        emit_routine_lines(batch);
        wait_for(index * batch, 0);
    }
    /* That many lines hold this many full bursts, whatever the wakes were. */
    const unsigned expected = (batch * batches) / BURST;
    wait_for(batch * batches, expected);
    CHECK(event_test_pauses() == expected);
    CHECK(event_test_last_pause_ms() == CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS);
    stop_drain();
}

/* A burst is at most its size, even when a wake finds part of one already used. */
static void pause_comes_at_the_burst_size(void)
{
    start_drain();
    emit_routine_lines(BURST - 1u);
    wait_for(BURST - 1u, 0);
    CHECK(event_test_pauses() == 0);
    /* One more line reaches the count. The others wait behind the pause. */
    emit_routine_lines(1);
    wait_for(BURST, 1);
    CHECK(event_test_pauses() == 1);
    stop_drain();
}

/* An idle timeout starts a new count, because the log buffer has had a whole wait to empty. */
static void idle_timeout_restarts_the_count(void)
{
    start_drain();
    emit_routine_lines(BURST - 1u);
    wait_for(BURST - 1u, 0);
    event_test_signal_next_reports(CRUX_ERR_TIMEOUT);
    /* Any later wake takes the report. After it, a whole burst's room is free again. */
    emit_routine_lines(BURST - 1u);
    wait_for(2u * (BURST - 1u), 0);
    CHECK(event_test_pauses() == 0);
    stop_drain();
}

/* A wait that fails pauses rather than spins, and that pause does not end the burst. */
static void failed_wait_pauses_and_keeps_the_count(void)
{
    start_drain();
    emit_routine_lines(1);
    wait_for(1, 0);
    wait_for_idle();
    CHECK(event_test_pauses() == 0);
    event_test_signal_next_reports(CRUX_ERR_IO);
    emit_routine_lines(1);
    wait_for(2, 1);
    wait_for_idle();
    CHECK(event_test_pauses() == 1);
    /* Two lines are used, so the burst's pause comes after the rest of them. */
    emit_routine_lines(BURST - 3u);
    wait_for(BURST - 1u, 1);
    wait_for_idle();
    CHECK(event_test_pauses() == 1);
    emit_routine_lines(1);
    wait_for(BURST, 2);
    CHECK(event_test_pauses() == 2);
    stop_drain();
}

/* One report in a burst, across wakes. A later loss waits for the next burst, and is not lost. */
static void one_report_in_a_burst_across_wakes(void)
{
    start_drain();
    /* A loss sends no signal, and the record that follows wakes the drain for both. */
    crux_security_event_emit(NULL);
    emit_routine_lines(1);
    wait_for(2, 0);
    wait_for_idle();
    CHECK(report_lines() == 1);

    /* A new wake in the same burst, with a new loss: only the record is written. */
    crux_security_event_emit(NULL);
    emit_routine_lines(1);
    wait_for(3, 0);
    wait_for_idle();
    CHECK(event_test_line_count() == 3);
    CHECK(report_lines() == 1);

    /* The burst's last line brings the pause, and the waiting report opens the next burst. */
    emit_routine_lines(BURST - 3u);
    wait_for(BURST + 1u, 1);
    wait_for_idle();
    CHECK(event_test_line_count() == BURST + 1u);
    CHECK(event_test_pauses() == 1);
    CHECK(report_lines() == 2);
    CHECK(strncmp(event_test_last_line(), "evt=event.lost ", 15) == 0);
    stop_drain();
}

/* An idle timeout starts a new burst, so a new report may be written in it. */
static void idle_timeout_allows_a_report(void)
{
    start_drain();
    crux_security_event_emit(NULL);
    emit_routine_lines(1);
    wait_for(2, 0);
    wait_for_idle();
    CHECK(report_lines() == 1);
    crux_security_event_emit(NULL);
    event_test_signal_next_reports(CRUX_ERR_TIMEOUT);
    emit_routine_lines(1);
    wait_for(4, 0);
    wait_for_idle();
    CHECK(report_lines() == 2);
    CHECK(event_test_pauses() == 0);
    stop_drain();
}

int main(void)
{
    signal_on_admission();
    signal_wakes_the_drain();
    wait_is_bounded();
    signal_again_after_a_burst();
    pause_counts_across_wakes();
    pause_comes_at_the_burst_size();
    idle_timeout_restarts_the_count();
    failed_wait_pauses_and_keeps_the_count();
    one_report_in_a_burst_across_wakes();
    idle_timeout_allows_a_report();
    event_test_reset();
    return event_test_finish("Security event drain");
}
