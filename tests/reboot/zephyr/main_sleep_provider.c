/* The Zephyr-direct sleep port and worker of the reboot service, on native_sim over the real
 * kernel.
 *
 * This image replaces the one of the Crux sleep provider, whose suite
 * (tests/reboot/sleep_provider.c) wrapped crux_semaphore_take() to give a take's answers: timeout,
 * an error, or the semaphore. The port is k_msleep() now and has no take to wrap, so those nine
 * checks cannot run (PR description). What can be said of the new port is checked here instead: it
 * sleeps the time asked in simulated time, and a sleep that the kernel ended early is not one. The
 * worker's priority is read from its thread. */
#include <crux/access.h>

#include "reboot_port.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

struct k_thread *crux_reboot_test_worker(void);

/* The service calls these; this image links none of the real ports. */
crux_status_t crux_reboot_port_reset(void)
{
    return CRUX_ERR_NOT_SUPPORTED;
}
bool crux_reboot_port_events_pending(void)
{
    return false;
}
void crux_reboot_port_event(crux_reboot_event_t event, crux_access_session_t session)
{
    (void)event;
    (void)session;
}
crux_access_result_t crux_access_check(crux_access_session_t session, const char *permission)
{
    (void)session;
    (void)permission;
    return CRUX_ACCESS_ERR_DENIED;
}

ZTEST(reboot_sleep_provider, test_the_port_sleeps_the_time_asked)
{
    const int64_t before = k_uptime_get();

    zassert_true(crux_reboot_port_sleep(1000u));
    zassert_true(k_uptime_get() - before >= 1000);
    zassert_true(crux_reboot_port_sleep(50u));
    zassert_true(k_uptime_get() - before >= 1050);
    zassert_true(crux_reboot_port_sleep(0u));
}

static K_THREAD_STACK_DEFINE(waker_stack, 4096);
static struct k_thread waker;

static void wake(void *target, void *unused2, void *unused3)
{
    (void)unused2;
    (void)unused3;
    k_msleep(10);
    k_wakeup((k_tid_t)target);
}

ZTEST(reboot_sleep_provider, test_a_sleep_that_was_woken_early_is_not_one)
{
    const int64_t before = k_uptime_get();

    k_thread_create(&waker, waker_stack, K_THREAD_STACK_SIZEOF(waker_stack), wake, k_current_get(),
                    NULL, NULL, k_thread_priority_get(k_current_get()) - 1, 0, K_NO_WAIT);
    zassert_false(crux_reboot_port_sleep(1000u), "a woken sleep said that it slept");
    zassert_true(k_uptime_get() - before < 1000);
    k_thread_join(&waker, K_FOREVER);
}

static bool policy_never_runs(void *context, crux_reboot_refusal_t *refusal)
{
    (void)context;
    (void)refusal;
    return true;
}

ZTEST(reboot_sleep_provider, test_the_worker_runs_at_the_priority_of_kconfig)
{
    const crux_reboot_config_t config = {.policy_kind = CRUX_REBOOT_POLICY_CALLBACK,
                                         .policy = policy_never_runs};

    zassert_equal(crux_reboot_start(&config), CRUX_REBOOT_OK);
    zassert_equal(crux_reboot_start_worker(), CRUX_REBOOT_OK);
    zassert_equal(crux_reboot_start_worker(), CRUX_REBOOT_OK, "a second start must change nothing");
    zassert_equal(k_thread_priority_get((k_tid_t)crux_reboot_test_worker()),
                  CONFIG_CRUX_REBOOT_WORKER_PRIORITY);
    /* The default is today's: the LOW class of the Crux Thread port, 6. */
    zassert_equal(CONFIG_CRUX_REBOOT_WORKER_PRIORITY, 6);
}

ZTEST_SUITE(reboot_sleep_provider, NULL, NULL, NULL, NULL, NULL);
