/* What "a bounded wait for pending saves" means, shown on the real services: a network settings
 * save that is queued or running, and a configuration save that the web routes queue. The wait
 * sources are the two functions that those components provide, and the saves are the real ones, on
 * the real Configuration service over the fake provider. The worker that carries out a save is run
 * by the suite's sleep, inside the reboot service's wait, which is where another worker would run.
 *
 * Nothing is cut short. A save that ends during the wait is whole when the reset is called. A save
 * that does not end stops the reset and is left to finish. A save that is queued after the last
 * poll is not seen, which is a stated limit and is pinned here. */
#include "net_test.h"
#include "reboot_test.h"
#include "web_management_internal.h"

#include <crux/reboot.h>

void reboot_fakes_reset(void);

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTEND(bugprone-reserved-identifier)

static bool lock_fails;

/* The probes' own wait is 20 ms, as that of the server-thread calls of their services. */
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (lock_fails && timeout_ms == 20u) {
        return CRUX_ERR_TIMEOUT;
    }
    return __real_crux_mutex_lock(mutex, timeout_ms);
}

static bool network_probe(void *context)
{
    (void)context;
    return crux_network_settings_save_pending();
}

static bool configuration_probe(void *context)
{
    (void)context;
    return crux_web_management_save_pending();
}

/* What the policy callback does as a side effect on its first call, at the request, and on its
 * second call, at the check before the reset. */
static void (*first_check_effect)(void);
static void (*second_check_effect)(void);
static size_t policy_calls;

static bool policy(void *context, crux_reboot_refusal_t *refusal)
{
    (void)context;
    (void)refusal;
    ++policy_calls;
    if (policy_calls == 1u && first_check_effect != NULL) {
        first_check_effect();
    }
    if (policy_calls == 2u && second_check_effect != NULL) {
        second_check_effect();
    }
    return true;
}

/* In reboot_test.c, which this suite does not link: it boots Access with its own accounts. */
crux_reboot_state_t reboot_state_of(crux_access_session_t session)
{
    crux_reboot_state_t state;

    memset(&state, 0xA5, sizeof(state));
    CHECK(crux_reboot_get(session, &state) == CRUX_REBOOT_OK);
    return state;
}

static void start_everything(void)
{
    static const crux_reboot_wait_source_t sources[] = {{network_probe, NULL, true},
                                                        {configuration_probe, NULL, true}};
    crux_reboot_config_t config = {CRUX_REBOOT_POLICY_CALLBACK, policy, NULL, sources, 2u};

    net_start();
    CHECK(crux_web_management_init() == CRUX_WEB_OK);
    reboot_fakes_reset();
    policy_calls = 0u;
    first_check_effect = NULL;
    second_check_effect = NULL;
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    net_events_clear();
}

static void expect_whole_set(const crux_network_settings_t *set)
{
    crux_network_settings_t values;

    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&values, set));
}

/* A network save that the worker finishes during the wait. */

static size_t sleeps_seen;

static void finish_the_network_save(uint64_t elapsed_ms)
{
    (void)elapsed_ms;
    /* The delay is the first sleep, and the first step of the wait is the second. The save is run
     * by the other worker at the third, so that the wait has seen it busy twice. */
    if (++sleeps_seen == 3u) {
        CHECK(crux_network_settings_work_once());
    }
}

static void a_network_save_is_waited_for(void)
{
    start_everything();
    CHECK(crux_network_settings_request(s_admin, &net_set_a, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_save_pending());
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    sleeps_seen = 0u;
    reboot_sleep_hook(finish_the_network_save);
    CHECK(crux_reboot_work_once());
    /* Delay, two polls that found it busy, and then a poll that found nothing. */
    CHECK(reboot_sleeps() == 3u);
    CHECK(!crux_network_settings_save_pending());
    CHECK(reboot_reset_calls() == 1u);
    /* The reset came after a save that is whole in the store, which a boot would apply. */
    expect_whole_set(&net_set_a);
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == 1u);
}

/* A configuration save that the web worker finishes during the wait. */

static void finish_the_configuration_save(uint64_t elapsed_ms)
{
    (void)elapsed_ms;
    if (++sleeps_seen == 2u) {
        CHECK(web_mgmt_work_once());
    }
}

/* Marks the one ordinary parameter, so that a save has something to write. */
static void mark_the_limit(const char *text)
{
    crux_config_service_item_t item;

    CHECK(crux_config_service_update(s_admin, NET_CFG_LIMIT, text, strlen(text), &item) ==
          CRUX_CONFIG_SERVICE_OK);
}

static void a_configuration_save_is_waited_for(void)
{
    size_t writes_before;

    start_everything();
    mark_the_limit("77");
    CHECK(web_mgmt_save_queue(s_admin) == WEB_SAVE_QUEUE_OK);
    CHECK(crux_web_management_save_pending());
    writes_before = config_fault_writes();
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    sleeps_seen = 0u;
    reboot_sleep_hook(finish_the_configuration_save);
    CHECK(crux_reboot_work_once());
    /* The delay, one poll that found it busy, and then a poll that found nothing. */
    CHECK(reboot_sleeps() == 2u);
    CHECK(!crux_web_management_save_pending());
    CHECK(web_mgmt_save_view().state == WEB_SAVE_SAVED);
    CHECK(config_fault_writes() > writes_before);
    CHECK(reboot_reset_calls() == 1u);
}

/* A save that is accepted while the wait is running is waited for as well: a network save is
 * already queued, and when it ends the wait goes on for a configuration save that was queued
 * meanwhile. */

static void finish_one_and_queue_the_next(uint64_t elapsed_ms)
{
    (void)elapsed_ms;
    ++sleeps_seen;
    if (sleeps_seen == 3u) {
        CHECK(crux_network_settings_work_once());
        CHECK(web_mgmt_save_queue(s_admin) == WEB_SAVE_QUEUE_OK);
    } else if (sleeps_seen == 4u) {
        CHECK(web_mgmt_work_once());
    }
}

static void a_save_accepted_during_the_wait_is_waited_for(void)
{
    start_everything();
    mark_the_limit("79");
    CHECK(crux_network_settings_request(s_admin, &net_set_b, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    sleeps_seen = 0u;
    reboot_sleep_hook(finish_one_and_queue_the_next);
    CHECK(crux_reboot_work_once());
    CHECK(sleeps_seen == 4u);
    CHECK(!crux_network_settings_save_pending() && !crux_web_management_save_pending());
    CHECK(web_mgmt_save_view().state == WEB_SAVE_SAVED);
    CHECK(reboot_reset_calls() == 1u);
    expect_whole_set(&net_set_b);
}

/* A save that does not end. */

static void a_save_that_never_ends_stops_the_reset_and_is_not_lost(void)
{
    crux_reboot_state_t state;

    start_everything();
    CHECK(crux_network_settings_request(s_admin, &net_set_a, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    /* The whole wait was used, and then nothing was reset. */
    CHECK(reboot_slept_ms() == 1000u + 15000u);
    CHECK(reboot_reset_calls() == 0u);
    state = reboot_state_of(s_admin);
    CHECK(state.state == CRUX_REBOOT_STATE_FAILED);
    CHECK(state.failure == CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT);
    CHECK(reboot_events_of(CRUX_REBOOT_EVENT_FAILED) == 1u);
    /* The save was only asked about. It is still queued, and it still completes, whole. */
    CHECK(crux_network_settings_save_pending());
    CHECK(config_fault_writes() == 0u);
    CHECK(crux_network_settings_work_once());
    expect_whole_set(&net_set_a);
    /* A new request, now that nothing is pending, resets. */
    reboot_sleep_hook(NULL);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 1u);
}

static void a_configuration_save_that_never_ends_stops_the_reset(void)
{
    start_everything();
    mark_the_limit("78");
    CHECK(web_mgmt_save_queue(s_admin) == WEB_SAVE_QUEUE_OK);
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_reset_calls() == 0u);
    CHECK(reboot_state_of(s_admin).failure == CRUX_REBOOT_FAILURE_SAVE_IN_FLIGHT);
    CHECK(crux_web_management_save_pending());
    CHECK(web_mgmt_work_once());
    CHECK(web_mgmt_save_view().state == WEB_SAVE_SAVED);
}

/* A save that is started while the policy is asked at the request is waited for like any other: the
 * wait polls after the delay, and not before the policy. */

static void queue_a_network_save_now(void);

static void a_save_started_during_the_first_check_is_waited_for(void)
{
    start_everything();
    first_check_effect = queue_a_network_save_now;
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    CHECK(crux_network_settings_save_pending());
    sleeps_seen = 0u;
    reboot_sleep_hook(finish_the_network_save);
    CHECK(crux_reboot_work_once());
    CHECK(reboot_sleeps() == 3u);
    CHECK(!crux_network_settings_save_pending());
    CHECK(reboot_reset_calls() == 1u);
    expect_whole_set(&net_set_a);
}

/* The limit: a save that is queued after the last poll. */

static void queue_a_save(void)
{
    CHECK(crux_network_settings_request(s_admin, &net_set_a, NULL) == CRUX_NETWORK_OK);
}

static void queue_a_network_save_now(void)
{
    queue_a_save();
}

static void a_save_queued_after_the_last_poll_is_not_seen(void)
{
    start_everything();
    second_check_effect = queue_a_save;
    CHECK(crux_reboot_request(s_admin, NULL) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_work_once());
    /* The second policy check is after the last poll, so a save that is accepted there is not
     * waited for, and the reset is called with it still queued. The reboot service says so, in its
     * limits. */
    CHECK(reboot_reset_calls() == 1u);
    CHECK(crux_network_settings_save_pending());
    CHECK(config_fault_writes() == 0u);
}

/* The probes themselves. */

static void the_probes_follow_their_services(void)
{
    /* Before anything starts, there is nothing to wait for. */
    CHECK(!crux_network_settings_save_pending());
    CHECK(!crux_web_management_save_pending());
    start_everything();
    CHECK(!crux_network_settings_save_pending() && !crux_web_management_save_pending());
    CHECK(crux_network_settings_request(s_admin, &net_set_a, NULL) == CRUX_NETWORK_OK);
    CHECK(crux_network_settings_save_pending());
    CHECK(crux_network_settings_work_once());
    CHECK(!crux_network_settings_save_pending());
    CHECK(web_mgmt_save_queue(s_admin) == WEB_SAVE_QUEUE_OK);
    CHECK(crux_web_management_save_pending());
    CHECK(web_mgmt_work_once());
    CHECK(!crux_web_management_save_pending());
}

static void a_lock_that_is_late_reads_as_pending(void)
{
    /* The safe answer: a probe that cannot tell does not say that nothing is in flight. */
    start_everything();
    CHECK(!crux_network_settings_save_pending() && !crux_web_management_save_pending());
    lock_fails = true;
    CHECK(crux_network_settings_save_pending());
    CHECK(crux_web_management_save_pending());
    lock_fails = false;
    CHECK(!crux_network_settings_save_pending() && !crux_web_management_save_pending());
}

int main(void)
{
    net_run_case("the probes follow their services", the_probes_follow_their_services);
    net_run_case("a network save is waited for", a_network_save_is_waited_for);
    net_run_case("a configuration save is waited for", a_configuration_save_is_waited_for);
    net_run_case("a save accepted during the wait is waited for",
                 a_save_accepted_during_the_wait_is_waited_for);
    net_run_case("a save that never ends stops the reset and is not lost",
                 a_save_that_never_ends_stops_the_reset_and_is_not_lost);
    net_run_case("a configuration save that never ends stops the reset",
                 a_configuration_save_that_never_ends_stops_the_reset);
    net_run_case("a save started during the first check is waited for",
                 a_save_started_during_the_first_check_is_waited_for);
    net_run_case("a save queued after the last poll is not seen",
                 a_save_queued_after_the_last_poll_is_not_seen);
    net_run_case("a lock that is late reads as pending", a_lock_that_is_late_reads_as_pending);
    return net_finish("reboot_pending");
}
