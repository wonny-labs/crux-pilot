/* The drain's thread: what wakes it, how it paces lines, and how long it waits. This core's timed
 * wake never fires in a run, so a line that the signal does not reach simply never appears. */
#include "access_check_stub.h"
#include "event_test.h"

#include <time.h>

#define BURST ((unsigned)CRUX_SECURITY_EVENT_DRAIN_BURST)
#define ROUTINE_ROOM ((size_t)CRUX_SECURITY_EVENT_RING - CRUX_SECURITY_EVENT_RESERVE)
#define WAIT_LIMIT_MS 10000u

/* Not crux_thread_sleep_for, which counts as a pause of the drain in this composition. */
static void rest_1_ms(void)
{
    const struct timespec rest = {0, 1000000};
    (void)nanosleep(&rest, NULL);
}

/* Waits for at least this many lines and pauses, and then for the drain to be idle again. */
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

/* A wait that fails pauses rather than spins, and keeps the count. */
static void failed_wait_pauses(void)
{
    start_drain();
    emit_routine_lines(1);
    wait_for(1, 0);
    CHECK(event_test_pauses() == 0);
    event_test_signal_next_reports(CRUX_ERR_IO);
    emit_routine_lines(1);
    wait_for(2, 1);
    CHECK(event_test_pauses() >= 1);
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
    failed_wait_pauses();
    event_test_reset();
    return event_test_finish("Security event drain");
}
