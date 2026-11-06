/* Requests, reads and the worker on several threads at once. What a reader sees is always a
 * consistent state, one request at a time is accepted, and what ends up stored is a whole set. */
#include "net_test.h"

#include <pthread.h>

static volatile bool stop;
static volatile unsigned accepted_a, accepted_b, busy_answers;
static volatile unsigned bad_states;

static void *worker_main(void *argument)
{
    (void)argument;
    while (!stop) {
        (void)crux_network_settings_work_once();
    }
    while (crux_network_settings_work_once()) {
    }
    return NULL;
}

static void *requester_main(void *argument)
{
    bool first = *(const bool *)argument;

    for (unsigned round = 0; round < 300u; ++round) {
        const crux_network_settings_t *set =
            ((round & 1u) != 0u) == first ? &net_set_a : &net_set_b;
        crux_network_result_t result = crux_network_settings_request(s_both, set, NULL);

        if (result == CRUX_NETWORK_OK) {
            __atomic_add_fetch(set == &net_set_a ? &accepted_a : &accepted_b, 1u, __ATOMIC_RELAXED);
        } else if (result == CRUX_NETWORK_ERR_BUSY) {
            __atomic_add_fetch(&busy_answers, 1u, __ATOMIC_RELAXED);
        } else {
            __atomic_add_fetch(&bad_states, 1u, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void *reader_main(void *argument)
{
    (void)argument;
    while (!stop) {
        crux_network_state_t state;

        if (crux_network_settings_get(s_reader, &state) != CRUX_NETWORK_OK) {
            continue;
        }
        /* Whole sets only, and never a failure here, because every write succeeds. */
        if (state.save == CRUX_NETWORK_SAVE_FAILED ||
            (state.has_saved && !net_same(&state.saved, &net_set_a) &&
             !net_same(&state.saved, &net_set_b)) ||
            !state.has_running || !net_same(&state.running, &net_defaults)) {
            __atomic_add_fetch(&bad_states, 1u, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void many_threads(void)
{
    pthread_t worker, requesters[3], readers[2];
    static const bool firsts[3] = {true, false, true};
    crux_network_settings_t values;

    net_start();
    CHECK(pthread_create(&worker, NULL, worker_main, NULL) == 0);
    for (size_t index = 0; index < 2; ++index) {
        CHECK(pthread_create(&readers[index], NULL, reader_main, NULL) == 0);
    }
    for (size_t index = 0; index < 3; ++index) {
        CHECK(pthread_create(&requesters[index], NULL, requester_main, (void *)&firsts[index]) ==
              0);
    }
    for (size_t index = 0; index < 3; ++index) {
        CHECK(pthread_join(requesters[index], NULL) == 0);
    }
    stop = true;
    CHECK(pthread_join(worker, NULL) == 0);
    for (size_t index = 0; index < 2; ++index) {
        CHECK(pthread_join(readers[index], NULL) == 0);
    }
    CHECK(bad_states == 0u);
    CHECK(accepted_a + accepted_b > 0u && busy_answers > 0u);
    /* Every accepted request was saved, and each save ended with one event. */
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_CHANGED) == accepted_a + accepted_b);
    CHECK(net_events_of(CRUX_NETWORK_SETTINGS_EVENT_SAVE_FAILED) == 0u);
    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_SAVED);
    CHECK(net_same(&values, &net_set_a) || net_same(&values, &net_set_b));
}

int main(void)
{
    net_run_case("requests, reads and the worker on several threads", many_threads);
    return net_finish("network_settings_concurrency");
}
