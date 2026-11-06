/* The service: the policy statement, permission, the state it shows, the first policy check and
 * what a refusal leaves, a request made while another is pending, and the events. On the real
 * Access core, with the fake ports. */
#include "reboot_test.h"

static crux_reboot_result_t request(crux_access_session_t session)
{
    char reason[CRUX_REBOOT_REASON_SIZE];

    memset(reason, 0xA5, sizeof(reason));
    return crux_reboot_request(session, reason);
}

static void an_unset_policy_fails_the_start(void)
{
    crux_reboot_config_t config = reboot_config();
    crux_reboot_state_t state;

    reboot_boot();
    /* A zeroed configuration states nothing. */
    CHECK(crux_reboot_start(&(crux_reboot_config_t){0}) == CRUX_REBOOT_ERR_NO_POLICY);
    /* A callback kind with no callback states nothing either. */
    config.policy = NULL;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_ERR_NO_POLICY);
    /* Neither does a kind that is not one of the two. */
    config = reboot_config();
    {
        /* A value that is not a kind, which an enum cast would not let the analyser see. */
        const int not_a_kind = 7;

        memcpy(&config.policy_kind, &not_a_kind, sizeof(config.policy_kind));
    }
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_ERR_NO_POLICY);
    /* A callback beside the always-allow option is two statements, and it is refused. */
    config = reboot_config();
    config.policy_kind = CRUX_REBOOT_POLICY_ALWAYS_ALLOW;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_ERR_INVALID_ARGUMENT);
    CHECK(crux_reboot_start(NULL) == CRUX_REBOOT_ERR_INVALID_ARGUMENT);
    /* A refused start leaves a service that is not there: no request, no read, no worker. */
    CHECK(request(s_admin) == CRUX_REBOOT_ERR_NOT_READY);
    CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_ERR_NOT_READY);
    CHECK(crux_reboot_start_worker() == CRUX_REBOOT_ERR_NOT_READY);
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_trace_count() == 0u);
}

static void wait_sources_are_checked_at_the_start(void)
{
    crux_reboot_config_t config = reboot_config();
    crux_reboot_wait_source_t nine[CRUX_REBOOT_WAIT_SOURCE_MAX + 1u];

    reboot_boot();
    for (size_t index = 0; index < CRUX_REBOOT_WAIT_SOURCE_MAX + 1u; ++index) {
        nine[index] = config.wait_sources[0];
    }
    config.wait_sources = nine;
    config.wait_source_count = CRUX_REBOOT_WAIT_SOURCE_MAX + 1u;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_ERR_INVALID_ARGUMENT);
    /* A source with no function. */
    config.wait_source_count = CRUX_REBOOT_WAIT_SOURCE_MAX;
    nine[3].busy = NULL;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_ERR_INVALID_ARGUMENT);
    nine[3] = nine[0];
    /* A count with no array. */
    config.wait_sources = NULL;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_ERR_INVALID_ARGUMENT);
    CHECK(request(s_admin) == CRUX_REBOOT_ERR_NOT_READY);
    /* Eight is allowed. */
    config.wait_sources = nine;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
}

static void a_callback_policy_starts(void)
{
    reboot_start();
    /* A second start changes nothing, and does not need the policy again. */
    CHECK(crux_reboot_start(&(crux_reboot_config_t){0}) == CRUX_REBOOT_OK);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_NONE);
}

static void the_always_allow_option_needs_no_callback(void)
{
    const crux_reboot_config_t config = {CRUX_REBOOT_POLICY_ALWAYS_ALLOW, NULL, NULL, NULL, 0u};

    reboot_boot();
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(reboot_policy_calls() == 0u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 1u);
    CHECK(crux_reboot_work_once());
    /* The reset ran, with no callback to ask, and the second check allowed it. */
    CHECK(reboot_reset_calls() == 1u);
    CHECK(reboot_policy_calls() == 0u);
}

static void nothing_works_before_the_start(void)
{
    crux_reboot_state_t state;

    reboot_boot();
    CHECK(request(s_admin) == CRUX_REBOOT_ERR_NOT_READY);
    CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_ERR_NOT_READY);
    CHECK(crux_reboot_start_worker() == CRUX_REBOOT_ERR_NOT_READY);
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_trace_count() == 0u);
}

static void a_request_needs_the_permission(void)
{
    size_t denials;

    reboot_start();
    denials = reboot_denials();
    CHECK(request(s_reader) == CRUX_REBOOT_ERR_DENIED);
    CHECK(request(s_stranger) == CRUX_REBOOT_ERR_DENIED);
    /* A forced password change holds everything back. */
    CHECK(request(s_forced) == CRUX_REBOOT_ERR_DENIED);
    CHECK(request(s_never_valid) == CRUX_REBOOT_ERR_SESSION_INVALID);
    CHECK(reboot_denials() == denials + 3u);
    /* The policy is not asked for a session that may not ask, nothing is queued, and no event of
     * this service is recorded: Access's denial is the record. */
    CHECK(reboot_policy_calls() == 0u);
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_trace_count() == 0u);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_NONE);
    /* Both sessions that hold it may. */
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
}

static void a_read_needs_the_permission(void)
{
    crux_reboot_state_t state = {0};
    size_t denials;

    reboot_start();
    CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_get(s_rebooter, &state) == CRUX_REBOOT_OK);
    denials = reboot_denials();
    CHECK(crux_reboot_get(s_reader, &state) == CRUX_REBOOT_ERR_DENIED);
    CHECK(crux_reboot_get(s_stranger, &state) == CRUX_REBOOT_ERR_DENIED);
    CHECK(crux_reboot_get(s_forced, &state) == CRUX_REBOOT_ERR_DENIED);
    CHECK(crux_reboot_get(s_never_valid, &state) == CRUX_REBOOT_ERR_SESSION_INVALID);
    CHECK(reboot_denials() == denials + 3u);
    /* A refusal leaves the output alone. */
    memset(&state, 0x5A, sizeof(state));
    CHECK(crux_reboot_get(s_stranger, &state) == CRUX_REBOOT_ERR_DENIED);
    CHECK(((const uint8_t *)&state)[0] == 0x5A);
    CHECK(crux_reboot_get(s_admin, NULL) == CRUX_REBOOT_ERR_INVALID_ARGUMENT);
    CHECK(reboot_trace_count() == 0u);
}

static void an_accepted_request_is_pending_and_recorded(void)
{
    crux_reboot_state_t state;

    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_PENDING);
    CHECK(state.reason[0] == '\0' && state.failure == CRUX_REBOOT_FAILURE_NONE);
    /* One policy call, then one event for the session that asked, and nothing else yet. */
    CHECK(reboot_trace_count() == 2u);
    CHECK(reboot_trace_get(0)->kind == STEP_POLICY && reboot_trace_get(0)->value == 1);
    CHECK(reboot_trace_get(1)->kind == STEP_EVENT &&
          reboot_trace_get(1)->value == CRUX_REBOOT_EVENT_REQUESTED);
    CHECK(reboot_trace_get(1)->session.value == s_rebooter.value);
    CHECK(reboot_reset_calls() == 0u && reboot_sleeps() == 0u);
}

static void the_first_check_can_refuse(void)
{
    char reason[CRUX_REBOOT_REASON_SIZE];
    crux_reboot_state_t state;

    reboot_start();
    reboot_policy_always(false);
    reboot_policy_reason("Machine cycle running", 22u);
    CHECK(crux_reboot_request(s_rebooter, reason) == CRUX_REBOOT_ERR_REFUSED);
    CHECK(strcmp(reason, "Machine cycle running") == 0);
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_REFUSED);
    CHECK(strcmp(state.reason, "Machine cycle running") == 0);
    CHECK(state.failure == CRUX_REBOOT_FAILURE_NONE);
    /* The refusal is recorded once, for the session that asked, and nothing was queued. */
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 0u);
    CHECK(reboot_event_session(CRUX_REBOOT_EVENT_REFUSED).value == s_rebooter.value);
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_sleeps() == 0u && reboot_reset_calls() == 0u);
    /* The reason is optional for a caller that does not show it. */
    CHECK(crux_reboot_request(s_rebooter, NULL) == CRUX_REBOOT_ERR_REFUSED);
}

static void a_later_request_replaces_a_refusal(void)
{
    const bool script[] = {false, true};
    char reason[CRUX_REBOOT_REASON_SIZE];
    crux_reboot_state_t state;

    reboot_start();
    reboot_policy_script(script, 2u);
    reboot_policy_reason("Not now", 8u);
    CHECK(crux_reboot_request(s_rebooter, reason) == CRUX_REBOOT_ERR_REFUSED);
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_REFUSED);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_PENDING && state.reason[0] == '\0');
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 1u);
}

static void a_request_while_pending_runs_no_policy_and_starts_nothing(void)
{
    reboot_start();
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(reboot_policy_calls() == 1u);
    /* Even a policy that has begun to refuse: the pending request's own second check decides. */
    reboot_policy_always(false);
    CHECK(request(s_admin) == CRUX_REBOOT_OK);
    CHECK(request(s_rebooter) == CRUX_REBOOT_OK);
    CHECK(reboot_policy_calls() == 0u); /* The script was reset, and nothing has called it since. */
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_PENDING);
    /* Each accepted request is recorded, and only one job exists. */
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 3u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 0u);
    /* The permission still applies to a duplicate. */
    CHECK(request(s_reader) == CRUX_REBOOT_ERR_DENIED);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 3u);
    reboot_policy_always(true);
    CHECK(crux_reboot_work_once());
    CHECK(!crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

static unsigned nested_calls;

/* A policy that refuses, and that is asked while another request is being accepted: the first call
 * makes a second request, which the second call allows, and then refuses its own. */
static bool refuse_while_another_is_accepted(void *context, crux_reboot_refusal_t *refusal)
{
    (void)context;
    if (++nested_calls == 1u) {
        CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
        memcpy(refusal->reason, "Late", 5u);
        return false;
    }
    return true;
}

static void a_refusal_does_not_cancel_a_request_accepted_meanwhile(void)
{
    const crux_reboot_config_t config = {CRUX_REBOOT_POLICY_CALLBACK,
                                         refuse_while_another_is_accepted, NULL, NULL, 0u};
    char reason[CRUX_REBOOT_REASON_SIZE];

    reboot_boot();
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    nested_calls = 0u;
    CHECK(crux_reboot_request(s_rebooter, reason) == CRUX_REBOOT_ERR_REFUSED);
    CHECK(strcmp(reason, "Late") == 0);
    /* The request that was accepted meanwhile keeps its place, and its job runs. */
    CHECK(reboot_state_of(s_admin).state == CRUX_REBOOT_STATE_PENDING);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REQUESTED) == 1u);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_REFUSED) == 1u);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

int main(void)
{
    reboot_run_case("an unset policy fails the start", an_unset_policy_fails_the_start);
    reboot_run_case("wait sources are checked at the start", wait_sources_are_checked_at_the_start);
    reboot_run_case("a callback policy starts", a_callback_policy_starts);
    reboot_run_case("the always-allow option needs no callback",
                    the_always_allow_option_needs_no_callback);
    reboot_run_case("nothing works before the start", nothing_works_before_the_start);
    reboot_run_case("a request needs the permission", a_request_needs_the_permission);
    reboot_run_case("a read needs the permission", a_read_needs_the_permission);
    reboot_run_case("an accepted request is pending and recorded",
                    an_accepted_request_is_pending_and_recorded);
    reboot_run_case("the first check can refuse", the_first_check_can_refuse);
    reboot_run_case("a later request replaces a refusal", a_later_request_replaces_a_refusal);
    reboot_run_case("a request while pending runs no policy and starts nothing",
                    a_request_while_pending_runs_no_policy_and_starts_nothing);
    reboot_run_case("a refusal does not cancel a request accepted meanwhile",
                    a_refusal_does_not_cancel_a_request_accepted_meanwhile);
    return reboot_finish("reboot_service");
}
