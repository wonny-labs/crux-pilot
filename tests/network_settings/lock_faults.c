/* A lock of the service that does not come in time. The server-thread calls then answer that the
 * service is busy, and the worker's step gives up without losing the queued save. The lock is the
 * service's own: --wrap=crux_mutex_lock fails the one wait of the length it uses for the state. */
#include "net_test.h"

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_status_t __real_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms);
// NOLINTEND(bugprone-reserved-identifier)

static uint32_t failing_wait; /* The timeout that fails, or 0 for none. */
static unsigned failures;

crux_status_t __wrap_crux_mutex_lock(crux_mutex_t mutex, uint32_t timeout_ms)
{
    if (failing_wait != 0u && timeout_ms == failing_wait) {
        ++failures;
        return CRUX_ERR_TIMEOUT;
    }
    return __real_crux_mutex_lock(mutex, timeout_ms);
}

static void the_server_thread_calls_say_busy(void)
{
    crux_network_state_t state;

    net_start();
    /* The state's lock is waited on for 20 ms by default, and the Access core waits for its own
     * length, which is a different one. */
    failing_wait = 20u;
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_ERR_BUSY);
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_ERR_BUSY);
    CHECK(failures == 2u);
    failing_wait = 0u;
    /* Nothing was queued by the refusal. */
    CHECK(!crux_network_settings_work_once());
    CHECK(crux_network_settings_get(s_both, &state) == CRUX_NETWORK_OK);
    CHECK(state.save == CRUX_NETWORK_SAVE_NONE);
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_OK);
}

static void the_worker_keeps_a_queued_save_when_its_lock_is_late(void)
{
    crux_network_settings_t values;

    net_start();
    CHECK(crux_network_settings_request(s_both, &net_set_a, NULL) == CRUX_NETWORK_OK);
    /* The worker waits 1000 ms for the operation lock first. A failure there leaves the job queued.
     */
    failing_wait = 1000u;
    CHECK(!crux_network_settings_work_once());
    failing_wait = 0u;
    CHECK(config_fault_writes() == 0u);
    CHECK(crux_network_settings_work_once());
    CHECK(net_reboot_decision(&values) == CRUX_NETWORK_BOOT_SAVED);
}

int main(void)
{
    net_run_case("the server-thread calls say busy", the_server_thread_calls_say_busy);
    net_run_case("the worker keeps a queued save when its lock is late",
                 the_worker_keeps_a_queued_save_when_its_lock_is_late);
    return net_finish("network_settings_lock_faults");
}
