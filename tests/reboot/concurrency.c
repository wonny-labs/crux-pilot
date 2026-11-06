/* Requests, reads and the worker on several threads at once, and a request or a read that comes
 * while the worker is in the middle of its wait. One job is queued at a time, a duplicate starts
 * nothing, a reader sees only states that exist, and every accepted request is recorded. */
#include "reboot_test.h"

#include <pthread.h>
#include <time.h>

static pthread_barrier_t barrier;

static void *post_main(void *argument)
{
    crux_access_session_t session = *(const crux_access_session_t *)argument;
    crux_reboot_result_t *result = malloc(sizeof(*result));

    pthread_barrier_wait(&barrier);
    *result = crux_reboot_request(session, NULL);
    return result;
}

static void two_posts_at_the_same_time_start_one_job(void)
{
    enum { ROUNDS = 200 };

    reboot_start();
    CHECK(pthread_barrier_init(&barrier, NULL, 2) == 0);
    for (unsigned round = 0; round < ROUNDS; ++round) {
        pthread_t threads[2];
        crux_access_session_t sessions[2] = {s_admin, s_rebooter};
        crux_reboot_result_t *results[2];
        size_t policy_before = reboot_policy_calls();

        for (size_t index = 0; index < 2; ++index) {
            CHECK(pthread_create(&threads[index], NULL, post_main, &sessions[index]) == 0);
        }
        for (size_t index = 0; index < 2; ++index) {
            CHECK(pthread_join(threads[index], (void **)&results[index]) == 0);
            CHECK(*results[index] == CRUX_REBOOT_OK);
            free(results[index]);
        }
        /* Both asked the policy, or the second found the first pending and did not. */
        CHECK(reboot_policy_calls() - policy_before >= 1u &&
              reboot_policy_calls() - policy_before <= 2u);
        CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_PENDING);
        /* One job, whatever the interleaving. */
        CHECK(crux_reboot_work_once());
        CHECK(!crux_reboot_work_once());
        CHECK(reboot_reset_calls() == round + 1u);
        CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == (size_t)2 * (round + 1u));
    }
    CHECK(pthread_barrier_destroy(&barrier) == 0);
}

static volatile bool stop;
static volatile unsigned accepted, refused, busy_answers, bad_answers, bad_states, overlaps;
static volatile unsigned in_sleep;

static void count_the_worker_in_its_sleeps(uint64_t elapsed_ms)
{
    (void)elapsed_ms;
    /* Only one worker may be inside its steps. */
    if (__atomic_add_fetch(&in_sleep, 1u, __ATOMIC_RELAXED) != 1u) {
        __atomic_add_fetch(&overlaps, 1u, __ATOMIC_RELAXED);
    }
    __atomic_sub_fetch(&in_sleep, 1u, __ATOMIC_RELAXED);
}

static void *requester_main(void *argument)
{
    crux_access_session_t session = *(const crux_access_session_t *)argument;

    for (unsigned round = 0; round < 300u; ++round) {
        crux_reboot_result_t result = crux_reboot_request(session, NULL);

        if (result == CRUX_REBOOT_OK) {
            __atomic_add_fetch(&accepted, 1u, __ATOMIC_RELAXED);
        } else if (result == CRUX_REBOOT_ERR_REFUSED) {
            __atomic_add_fetch(&refused, 1u, __ATOMIC_RELAXED);
        } else if (result == CRUX_REBOOT_ERR_BUSY) {
            __atomic_add_fetch(&busy_answers, 1u, __ATOMIC_RELAXED);
        } else {
            __atomic_add_fetch(&bad_answers, 1u, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void *reader_main(void *argument)
{
    (void)argument;
    while (!stop) {
        crux_reboot_state_t state;

        if (crux_reboot_get(s_admin, &state) != CRUX_REBOOT_OK) {
            continue;
        }
        bool has_reason = state.reason[0] != '\0';

        if ((state.state == CRUX_REBOOT_STATE_REFUSED) != has_reason ||
            (state.state != CRUX_REBOOT_STATE_FAILED &&
             state.failure != CRUX_REBOOT_FAILURE_NONE) ||
            (unsigned)state.state > (unsigned)CRUX_REBOOT_STATE_FAILED) {
            __atomic_add_fetch(&bad_states, 1u, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void requests_reads_and_the_real_worker(void)
{
    /* The policy refuses now and then, on the server's side and on the worker's. */
    static const bool script[] = {true, true, false, true, true, true, false, true};
    pthread_t requesters[3], readers[2];
    crux_access_session_t sessions[3];
    struct timespec nap = {0, 2000000L};

    reboot_start();
    reboot_policy_script(script, sizeof(script) / sizeof(script[0]));
    reboot_policy_reason("Not now", 8u);
    reboot_sleep_hook(count_the_worker_in_its_sleeps);
    CHECK(crux_reboot_start_worker() == CRUX_REBOOT_OK);
    CHECK(crux_reboot_start_worker() == CRUX_REBOOT_OK);
    sessions[0] = s_admin;
    sessions[1] = s_rebooter;
    sessions[2] = s_admin;
    for (size_t index = 0; index < 2; ++index) {
        CHECK(pthread_create(&readers[index], NULL, reader_main, NULL) == 0);
    }
    for (size_t index = 0; index < 3; ++index) {
        CHECK(pthread_create(&requesters[index], NULL, requester_main, &sessions[index]) == 0);
    }
    for (size_t index = 0; index < 3; ++index) {
        CHECK(pthread_join(requesters[index], NULL) == 0);
    }
    /* The worker is a real thread, so give it time to end the last job. */
    for (unsigned waited = 0; waited < 2500u; ++waited) {
        if (reboot_state_of(s_admin).state != CRUX_REBOOT_STATE_PENDING) {
            break;
        }
        nanosleep(&nap, NULL);
    }
    stop = true;
    for (size_t index = 0; index < 2; ++index) {
        CHECK(pthread_join(readers[index], NULL) == 0);
    }
    CHECK(reboot_state_of(s_admin).state != CRUX_REBOOT_STATE_PENDING);
    CHECK(bad_answers == 0u && bad_states == 0u && overlaps == 0u);
    CHECK(accepted > 0u && refused > 0u);
    /* Every accepted request is recorded once, and every job ended in one event. */
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == accepted);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == reboot_reset_calls());
    CHECK(reboot_reset_calls() >= 1u && reboot_reset_calls() <= accepted);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) >= refused);
    /* The refusals that the requests got are among the recorded ones. A job that the second check
     * refused adds one more. */
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) - refused <= accepted);
}

static unsigned hook_runs;
static bool hook_saw_pending;
static bool hook_duplicate_accepted;
static size_t hook_policy_before;
static size_t hook_policy_after;
static bool hook_read_denied_state_ok;
static bool hook_nested_worker_ran;

static void ask_during_the_wait(uint64_t elapsed_ms)
{
    crux_reboot_state_t state;

    ++hook_runs;
    if (hook_runs == 1u) {
        /* The delay: the worker is asleep inside the service and holds nothing that a request or
         * a read needs. */
        hook_policy_before = reboot_policy_calls();
        hook_saw_pending = crux_reboot_get(s_rebooter, &state) == CRUX_REBOOT_OK &&
                           state.state == CRUX_REBOOT_STATE_PENDING;
        hook_duplicate_accepted = crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK;
        /* A second worker that comes by while this one waits finds the job taken. */
        hook_nested_worker_ran = crux_reboot_work_once();
        hook_policy_after = reboot_policy_calls();
        hook_read_denied_state_ok = crux_reboot_get(s_reader, &state) == CRUX_REBOOT_ERR_DENIED;
    } else if (elapsed_ms > 1000u) {
        /* A step of the wait. */
        if (crux_reboot_get(s_rebooter, &state) != CRUX_REBOOT_OK ||
            state.state != CRUX_REBOOT_STATE_PENDING) {
            hook_saw_pending = false;
        }
        if (crux_reboot_request(s_admin, NULL) != CRUX_REBOOT_OK) {
            hook_duplicate_accepted = false;
        }
    }
}

static void a_post_and_a_get_while_the_worker_waits(void)
{
    reboot_start();
    CHECK(crux_reboot_request(s_rebooter, NULL) == CRUX_REBOOT_OK);
    reboot_source_busy(0u, 3u);
    reboot_sleep_hook(ask_during_the_wait);
    CHECK(crux_reboot_work_once());
    CHECK(hook_runs == 4u);
    CHECK(hook_saw_pending && hook_duplicate_accepted && hook_read_denied_state_ok);
    CHECK(!hook_nested_worker_ran);
    /* The duplicates ran no policy, started nothing, and were each recorded. */
    CHECK(hook_policy_after == hook_policy_before);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 5u);
    CHECK(reboot_reset_calls() == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
    /* They did not leave a second job behind. */
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

int main(void)
{
    reboot_run_case("two posts at the same time start one job",
                    two_posts_at_the_same_time_start_one_job);
    reboot_run_case("requests, reads and the real worker", requests_reads_and_the_real_worker);
    reboot_run_case("a post and a get while the worker waits",
                    a_post_and_a_get_while_the_worker_waits);
    return reboot_finish("reboot_concurrency");
}
