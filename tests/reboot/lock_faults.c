/* A lock of the service that does not come in time. The server-thread calls then answer that the
 * service is busy and change nothing, and the worker's step gives up without losing the job. The
 * lock is the service's own: --wrap=crux_mutex_lock fails the wait of the length it uses for the
 * state, and the Nth of them when a case names one. */
#include "reboot_test.h"

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTEND(bugprone-reserved-identifier)

static uint32_t failing_wait; /* The timeout that fails, or 0 for none. */
static unsigned failing_nth;  /* Only the Nth wait of that length, counted from 1, or 0 for all. */
static unsigned seen;
static unsigned failures;

crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (failing_wait != 0u && timeout_ms == failing_wait) {
        ++seen;
        if (failing_nth == 0u || seen == failing_nth) {
            ++failures;
            return CRUX_ERR_TIMEOUT;
        }
    }
    return __real_crux_mutex_lock(mutex, timeout_ms);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): the wait and the count differ in meaning
static void fail(uint32_t wait, unsigned nth)
{
    failing_wait = wait;
    failing_nth = nth;
    seen = 0u;
}

static void the_server_thread_calls_say_busy(void)
{
    crux_reboot_state_t state;

    reboot_start();
    fail(20u, 0u);
    CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_ERR_BUSY);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_ERR_BUSY);
    CHECK(failures == 2u);
    fail(0u, 0u);
    /* Nothing was queued, asked or recorded by the refusals. */
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_policy_calls() == 0u && reboot_trace_count() == 0u);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_NONE);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
}

static void a_lock_that_is_late_after_the_policy_changes_nothing(void)
{
    reboot_start();
    /* The first lock reads whether one is pending, and the second one records the outcome. */
    fail(20u, 2u);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_ERR_BUSY);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 0u);
    fail(0u, 0u);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_NONE);
    CHECK(!crux_reboot_work_once());
    /* The same for a refusal: no state and no event, and the refusal is not claimed. */
    reboot_policy_always(false);
    fail(20u, 2u);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_ERR_BUSY);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 0u);
    fail(0u, 0u);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_NONE);
}

static void the_worker_keeps_its_job_when_its_lock_is_late(void)
{
    reboot_start();
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    /* The worker waits 1000 ms to take the job. A failure there leaves it pending. */
    fail(1000u, 1u);
    CHECK(!crux_reboot_work_once());
    fail(0u, 0u);
    CHECK(reboot_sleeps() == 0u && reboot_reset_calls() == 0u);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_PENDING);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

static void the_worker_ends_its_job_when_the_lock_is_late_at_the_end(void)
{
    reboot_start();
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    /* The first wait of 1000 ms takes the job, and the second one ends it, and is tried again. */
    fail(1000u, 2u);
    CHECK(crux_reboot_work_once());
    CHECK(failures == 1u);
    fail(0u, 0u);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_FAILED);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
}

int main(void)
{
    reboot_run_case("the server-thread calls say busy", the_server_thread_calls_say_busy);
    reboot_run_case("a lock that is late after the policy changes nothing",
                    a_lock_that_is_late_after_the_policy_changes_nothing);
    reboot_run_case("the worker keeps its job when its lock is late",
                    the_worker_keeps_its_job_when_its_lock_is_late);
    reboot_run_case("the worker ends its job when the lock is late at the end",
                    the_worker_ends_its_job_when_the_lock_is_late_at_the_end);
    return reboot_finish("reboot_lock_faults");
}
