/* What the worker does with an accepted request, and in what order: the delay, the bounded wait for
 * events and saves, the second policy check, the reset. The sleep is virtual, so the bounds are
 * held to exact numbers. The numbers below are the documented defaults. A change to one of them is
 * a change to the documented behaviour and to this file. */
#include "reboot_test.h"

#define DELAY_MS 1000
#define SETTLE_MS 15000
#define POLL_MS 50
/* The polls of a wait that runs to its bound: one at 0 ms, one at each step, and one at the bound.
 */
#define POLLS_TO_THE_BOUND (SETTLE_MS / POLL_MS + 1)

static crux_reboot_result_t request(crux_access_session_t session)
{
    return crux_reboot_request(session, NULL);
}

static void expect(size_t index, reboot_step_kind_t kind, int64_t value)
{
    const reboot_step_t *step = reboot_trace_get(index);

    CHECK(step != NULL);
    if (step == NULL) {
        return;
    }
    if (step->kind != kind || step->value != value) {
        printf("  step %zu: expected kind %d value %lld, got kind %d value %lld\n", index,
               (int)kind, (long long)value, (int)step->kind, (long long)step->value);
    }
    CHECK(step->kind == kind);
    CHECK(step->value == value);
}

static void the_steps_come_in_order(void)
{
    size_t index = 0;

    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    /* A source that is busy for two polls, so that the wait is seen to step. */
    reboot_source_busy(1u, 2u);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_trace_count() == 17u);
    expect(index++, STEP_POLICY, 1);                          /* The first check, at the request. */
    expect(index++, STEP_EVENT, CRUX_REBOOT_EVENT_REQUESTED); /* Recorded before anything else. */
    expect(index++, STEP_SLEEP, DELAY_MS);                    /* The delay, so the 202 gets out. */
    expect(index++, STEP_EVENTS_PENDING, 0); /* A poll asks events, then each source. */
    expect(index++, STEP_BUSY, 0);
    expect(index++, STEP_BUSY, 3);
    expect(index++, STEP_SLEEP, POLL_MS);
    expect(index++, STEP_EVENTS_PENDING, 0);
    expect(index++, STEP_BUSY, 0);
    expect(index++, STEP_BUSY, 3);
    expect(index++, STEP_SLEEP, POLL_MS);
    expect(index++, STEP_EVENTS_PENDING, 0);
    expect(index++, STEP_BUSY, 0);
    expect(index++, STEP_BUSY, 2);
    expect(index++, STEP_POLICY, 1); /* The second check, after the wait and before the reset. */
    expect(index++, STEP_RESET, CRUX_ERR_NOT_SUPPORTED);
    expect(index++, STEP_EVENT, CRUX_REBOOT_EVENT_FAILED); /* Host: the reset returns. */
    CHECK(reboot_slept_ms() == DELAY_MS + 2 * POLL_MS);
    (void)index;
}

static void nothing_runs_before_the_worker(void)
{
    reboot_start();
    CHECK(request(s_admin) == CRUX_REBOOT_OK);
    CHECK(reboot_sleeps() == 0u && reboot_reset_calls() == 0u);
    CHECK(reboot_policy_calls() == 1u);
}

static void the_wait_ends_at_its_bound_and_the_reset_goes_ahead(void)
{
    /* Events never finish, and a source that does not block never finishes. */
    crux_reboot_config_t config = reboot_config();
    crux_reboot_wait_source_t source;

    reboot_boot();
    source = config.wait_sources[0];
    source.blocks_reset = false;
    config.wait_sources = &source;
    config.wait_source_count = 1u;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_events_pending_for(REBOOT_FOREVER);
    reboot_source_busy(0u, REBOOT_FOREVER);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_slept_ms() == DELAY_MS + SETTLE_MS);
    CHECK(reboot_sleeps() == 1u + SETTLE_MS / POLL_MS);
    CHECK(reboot_source_polls(0u) == (size_t)POLLS_TO_THE_BOUND);
    CHECK(reboot_trace_count_of(STEP_EVENTS_PENDING) == (size_t)POLLS_TO_THE_BOUND);
    /* The policy is asked again and the reset is called. */
    CHECK(reboot_policy_calls() == 2u && reboot_reset_calls() == 1u);
    CHECK(reboot_trace_get(reboot_trace_count() - 2u)->kind == STEP_RESET);
}

static void events_alone_never_hold_the_reset_back(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_events_pending_for(REBOOT_FOREVER);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_slept_ms() == DELAY_MS + SETTLE_MS);
    CHECK(reboot_reset_calls() == 1u);
}

static void a_source_that_frees_on_the_last_poll_is_waited_for(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    /* Busy for every poll but the last one, the one at the bound. */
    reboot_source_busy(1u, (size_t)POLLS_TO_THE_BOUND - 1u);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_source_polls(1u) == (size_t)POLLS_TO_THE_BOUND);
    CHECK(reboot_slept_ms() == DELAY_MS + SETTLE_MS);
    CHECK(reboot_reset_calls() == 1u);
    CHECK(reboot_policy_calls() == 2u);
    CHECK(reboot_state_of(s_admin).failure == CRUX_REBOOT_FAILURE_RESET_RETURNED);
}

static void a_blocking_source_that_is_still_busy_stops_the_reset(void)
{
    crux_reboot_state_t state;

    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    /* One poll more than the wait can make, so that the reset cannot be reached. */
    reboot_source_busy(1u, (size_t)POLLS_TO_THE_BOUND);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_slept_ms() == DELAY_MS + SETTLE_MS);
    CHECK(reboot_reset_calls() == 0u);
    CHECK(reboot_policy_calls() == 1u); /* No second check: nothing will be reset. */
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_FAILED);
    CHECK(state.failure == CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT);
    CHECK(state.reason[0] == '\0');
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
    CHECK(reboot_event_session(CRUX_REBOOT_EVENT_FAILED).value == s_rebooter.value);
    /* The work was left alone: its source was only asked. */
    CHECK(reboot_source_polls(1u) == (size_t)POLLS_TO_THE_BOUND);
}

static void the_other_blocking_source_counts_too(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, REBOOT_FOREVER);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 0u);
    CHECK(reboot_state_of(s_admin).failure == CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT);
}

static void a_source_that_does_not_block_may_be_cut(void)
{
    crux_reboot_config_t config = reboot_config();
    crux_reboot_wait_source_t sources[2];

    reboot_boot();
    sources[0] = config.wait_sources[0];
    sources[1] = config.wait_sources[1];
    sources[0].blocks_reset = false;
    config.wait_sources = sources;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, REBOOT_FOREVER);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

static void work_that_finishes_during_the_wait_is_waited_for(void)
{
    /* The wait does not end early, and it does not end late: the reset follows the first poll that
     * finds nothing pending. */
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, 7u);
    reboot_events_pending_for(3u);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_slept_ms() == DELAY_MS + 7 * POLL_MS);
    CHECK(reboot_source_polls(0u) == 8u);
    CHECK(reboot_reset_calls() == 1u);
}

static void the_second_check_can_refuse(void)
{
    const bool script[] = {true, false};
    crux_reboot_state_t state;

    reboot_start();
    reboot_policy_script(script, 2u);
    reboot_policy_reason("Machine cycle started", 22u);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    /* The wait ran, the second check refused, and the reset was never called. */
    CHECK(reboot_slept_ms() == DELAY_MS);
    CHECK(reboot_policy_calls() == 2u);
    CHECK(reboot_reset_calls() == 0u);
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_REFUSED);
    CHECK(strcmp(state.reason, "Machine cycle started") == 0);
    CHECK(state.failure == CRUX_REBOOT_FAILURE_NONE);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 0u);
    CHECK(reboot_event_session(CRUX_REBOOT_EVENT_REFUSED).value == s_rebooter.value);
    /* The request is over, so the worker has nothing more. */
    CHECK(!crux_reboot_work_once());
}

static void the_second_check_comes_after_the_wait(void)
{
    const bool script[] = {true, false};

    reboot_start();
    reboot_policy_script(script, 2u);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(1u, 4u);
    CHECK(crux_reboot_work_once());
    /* The second policy call is after the last poll, and the refusal is the last step. */
    size_t count = reboot_trace_count();
    CHECK(reboot_trace_get(count - 2u)->kind == STEP_POLICY);
    CHECK(reboot_trace_get(count - 2u)->value == 0);
    CHECK(reboot_trace_get(count - 3u)->kind == STEP_BUSY);
    CHECK(reboot_trace_get(count - 1u)->kind == STEP_EVENT);
}

static void a_reset_that_returns_is_a_failure(void)
{
    static const crux_status_t returns[] = {CRUX_ERR_NOT_SUPPORTED, CRUX_ERR_IO, CRUX_ERR_TIMEOUT,
                                            CRUX_OK};

    for (size_t index = 0; index < sizeof(returns) / sizeof(returns[0]); ++index) {
        crux_reboot_state_t state;

        reboot_start();
        reboot_reset_returns(returns[index]);
        CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
        CHECK(crux_reboot_work_once());
        CHECK(reboot_reset_calls() == 1u);
        state = reboot_state_of(s_admin);
        CHECK(state.state == CRUX_REBOOT_STATE_FAILED);
        CHECK(state.failure == CRUX_REBOOT_FAILURE_RESET_RETURNED);
        CHECK(state.reason[0] == '\0');
        CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
        CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 0u);
        CHECK(reboot_event_session(CRUX_REBOOT_EVENT_FAILED).value == s_rebooter.value);
    }
}

static void a_failed_request_can_be_made_again(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_FAILED);
    /* The next accepted request replaces the failure, and starts a new job. */
    CHECK(request(s_admin) == CRUX_REBOOT_OK);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_PENDING);
    CHECK(reboot_state_of(s_admin).failure == CRUX_REBOOT_FAILURE_NONE);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 2u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 2u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 2u);
}

static void a_session_that_ends_before_the_worker_does_not_cancel_the_request(void)
{
    /* Ending a session looks, to a browser, like a restart, so the request is not cancelled by it:
     * a cancelled request behind a lost session would be reported as done to nobody. */
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    crux_access_logout(&s_rebooter);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
}

static bool raced_work;
static bool raced_ran;

static void race_the_worker(crux_reboot_event_t event)
{
    if (event == CRUX_REBOOT_EVENT_REQUESTED && !raced_work) {
        raced_work = true;
        /* A worker that comes by while the event is still being recorded finds nothing to do. */
        raced_ran = crux_reboot_work_once();
    }
}

static void the_event_is_recorded_before_the_job_can_run(void)
{
    reboot_start();
    raced_work = false;
    raced_ran = true;
    reboot_event_hook(race_the_worker);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(raced_work && !raced_ran);
    CHECK(reboot_reset_calls() == 0u && reboot_sleeps() == 0u);
    /* Once the request has returned, the job is there. */
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

static void a_blocking_source_is_not_forgotten_by_a_later_one(void)
{
    crux_reboot_config_t config = reboot_config();
    crux_reboot_wait_source_t sources[2];

    reboot_boot();
    sources[0] = config.wait_sources[0];
    sources[1] = config.wait_sources[1];
    /* The first source blocks the reset and the second does not, and both stay busy for ever. The
     * second one's answer must not hide the first one's. */
    sources[1].blocks_reset = false;
    config.wait_sources = sources;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, REBOOT_FOREVER);
    reboot_source_busy(1u, REBOOT_FOREVER);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 0u);
    CHECK(reboot_state_of(s_admin).failure == CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT);
}

/* The end of a request is recorded before its state changes, as its start is: a state that a client
 * reads has its record behind it. The hook reads the state from inside each end event. */

static crux_reboot_state_kind_t state_at_end_event;
static bool end_event_seen;

static void read_the_state_at_the_end(crux_reboot_event_t event)
{
    crux_reboot_state_t state;

    if (event != CRUX_REBOOT_EVENT_REQUESTED) {
        end_event_seen = true;
        CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_OK);
        state_at_end_event = state.state;
    }
}

static void expect_the_state_still_pending_at_the_end_event(void)
{
    CHECK(end_event_seen);
    CHECK(state_at_end_event == CRUX_REBOOT_STATE_PENDING);
}

static void the_end_is_recorded_before_the_state_changes_after_a_failed_reset(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_event_hook(read_the_state_at_the_end);
    CHECK(crux_reboot_work_once());
    expect_the_state_still_pending_at_the_end_event();
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_FAILED);
}

static void the_end_is_recorded_before_the_state_changes_after_a_save_in_flight(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, REBOOT_FOREVER);
    reboot_event_hook(read_the_state_at_the_end);
    CHECK(crux_reboot_work_once());
    expect_the_state_still_pending_at_the_end_event();
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_FAILED);
}

static void the_end_is_recorded_before_the_state_changes_after_a_refusal(void)
{
    const bool script[] = {true, false};

    reboot_start();
    reboot_policy_script(script, 2u);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_event_hook(read_the_state_at_the_end);
    CHECK(crux_reboot_work_once());
    expect_the_state_still_pending_at_the_end_event();
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_REFUSED);
}

/* A sleep that cannot sleep: the delay and the bound of the wait would be lost, so the request
 * fails and the reset is not called. */

static void expect_failed_for_want_of_a_timer(void)
{
    crux_reboot_state_t state = reboot_state_of(s_admin);

    CHECK(reboot_reset_calls() == 0u);
    CHECK(state.state == CRUX_REBOOT_STATE_FAILED);
    CHECK(state.failure == CRUX_REBOOT_FAILURE_NO_TIMER);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
    CHECK(reboot_event_session(CRUX_REBOOT_EVENT_FAILED).value == s_rebooter.value);
    /* No second check: nothing will be reset. */
    CHECK(reboot_policy_calls() == 1u);
}

static void a_delay_that_cannot_sleep_fails_the_request(void)
{
    reboot_start();
    reboot_sleep_fails_from(1u);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    expect_failed_for_want_of_a_timer();
    CHECK(reboot_trace_count_of(STEP_EVENTS_PENDING) == 0u);
}

static void a_wait_that_cannot_sleep_fails_the_request(void)
{
    reboot_start();
    /* The delay sleeps, a save is pending, and the first step of the wait cannot. */
    reboot_sleep_fails_from(2u);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, 5u);
    CHECK(crux_reboot_work_once());
    expect_failed_for_want_of_a_timer();
    CHECK(reboot_source_polls(0u) == 1u);
}

static void a_sleep_that_is_never_needed_is_never_asked_for(void)
{
    reboot_start();
    /* Only the delay sleeps when nothing is pending, so a second sleep that would fail is not made.
     */
    reboot_sleep_fails_from(2u);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_sleeps() == 1u);
    CHECK(reboot_reset_calls() == 1u);
    CHECK(reboot_state_of(s_admin).failure == CRUX_REBOOT_FAILURE_RESET_RETURNED);
}

int main(void)
{
    reboot_run_case("the steps come in order", the_steps_come_in_order);
    reboot_run_case("nothing runs before the worker", nothing_runs_before_the_worker);
    reboot_run_case("the wait ends at its bound and the reset goes ahead",
                    the_wait_ends_at_its_bound_and_the_reset_goes_ahead);
    reboot_run_case("events alone never hold the reset back",
                    events_alone_never_hold_the_reset_back);
    reboot_run_case("a source that frees on the last poll is waited for",
                    a_source_that_frees_on_the_last_poll_is_waited_for);
    reboot_run_case("a blocking source that is still busy stops the reset",
                    a_blocking_source_that_is_still_busy_stops_the_reset);
    reboot_run_case("the other blocking source counts too", the_other_blocking_source_counts_too);
    reboot_run_case("a source that does not block may be cut",
                    a_source_that_does_not_block_may_be_cut);
    reboot_run_case("work that finishes during the wait is waited for",
                    work_that_finishes_during_the_wait_is_waited_for);
    reboot_run_case("the second check can refuse", the_second_check_can_refuse);
    reboot_run_case("the second check comes after the wait", the_second_check_comes_after_the_wait);
    reboot_run_case("a reset that returns is a failure", a_reset_that_returns_is_a_failure);
    reboot_run_case("a failed request can be made again", a_failed_request_can_be_made_again);
    reboot_run_case("a session that ends before the worker does not cancel the request",
                    a_session_that_ends_before_the_worker_does_not_cancel_the_request);
    reboot_run_case("the event is recorded before the job can run",
                    the_event_is_recorded_before_the_job_can_run);
    reboot_run_case("a blocking source is not forgotten by a later one",
                    a_blocking_source_is_not_forgotten_by_a_later_one);
    reboot_run_case("the end is recorded before the state changes after a failed reset",
                    the_end_is_recorded_before_the_state_changes_after_a_failed_reset);
    reboot_run_case("the end is recorded before the state changes after a save in flight",
                    the_end_is_recorded_before_the_state_changes_after_a_save_in_flight);
    reboot_run_case("the end is recorded before the state changes after a refusal",
                    the_end_is_recorded_before_the_state_changes_after_a_refusal);
    reboot_run_case("a delay that cannot sleep fails the request",
                    a_delay_that_cannot_sleep_fails_the_request);
    reboot_run_case("a wait that cannot sleep fails the request",
                    a_wait_that_cannot_sleep_fails_the_request);
    reboot_run_case("a sleep that is never needed is never asked for",
                    a_sleep_that_is_never_needed_is_never_asked_for);
    return reboot_finish("reboot_steps");
}
