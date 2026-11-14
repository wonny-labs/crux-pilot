/*
 * Per-service priorities of Crux's service workers (ADR-0002 P3), read back from the kernel.
 *
 * Each service worker is a Crux Thread at the LOW class, and its entry first calls
 * crux_service_priority_place() with its own identifier. This suite does the same from a Crux
 * Thread worker and reads the priority before and after, so it checks the class mapping, the
 * Kconfig value and the placement together. Which entry calls which identifier is checked
 * statically by tests/service_priority/check_placement.py, since the services need their whole
 * composition.
 */
#include "service_priority.h"

#include <crux/thread.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

typedef struct {
    crux_service_thread_t which;
    bool place;
    int before;
    int after;
} probe_t;

static void probe_main(void *argument)
{
    probe_t *probe = argument;
    probe->before = k_thread_priority_get(k_current_get());
    if (probe->place) {
        crux_service_priority_place(probe->which);
    }
    probe->after = k_thread_priority_get(k_current_get());
}

static probe_t run_probe(crux_thread_priority_t priority, crux_service_thread_t which, bool place)
{
    const crux_thread_options_t options = {0u, priority};
    probe_t probe = {which, place, -100, -100};
    crux_thread_t thread;
    zassert_equal(crux_thread_create(&options, probe_main, &probe, &thread), CRUX_OK);
    zassert_equal(crux_thread_join(&thread, 5000u), CRUX_OK);
    return probe;
}

/* The number each worker runs at in this scenario. The default scenario is today's: LOW, 6. */
static int expected(crux_service_thread_t which)
{
#ifdef CRUX_TEST_PLACED
    static const int placed[CRUX_SERVICE_THREAD_COUNT] = {
        [CRUX_SERVICE_THREAD_SECURITY_EVENT_DRAIN] = 9,
        [CRUX_SERVICE_THREAD_REBOOT_WORKER] = 0,
        [CRUX_SERVICE_THREAD_NETWORK_SETTINGS_WORKER] = 10,
        [CRUX_SERVICE_THREAD_WEB_SESSION_WORKER] = 11,
        [CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER] = 14,
    };
    return placed[which];
#else
    (void)which;
    return 6;
#endif
}

ZTEST(service_priority, test_class_mapping_is_unchanged)
{
    zassert_equal(run_probe(CRUX_THREAD_PRIORITY_HIGH, 0, false).before, 4);
    zassert_equal(run_probe(CRUX_THREAD_PRIORITY_NORMAL, 0, false).before, 5);
    zassert_equal(run_probe(CRUX_THREAD_PRIORITY_LOW, 0, false).before, 6);
}

ZTEST(service_priority, test_each_worker_runs_at_its_setting)
{
    for (int which = 0; which < CRUX_SERVICE_THREAD_COUNT; ++which) {
        probe_t probe = run_probe(CRUX_THREAD_PRIORITY_LOW, (crux_service_thread_t)which, true);
        zassert_equal(probe.before, 6, "worker %d did not start at the LOW class", which);
        zassert_equal(probe.after, expected((crux_service_thread_t)which), "worker %d runs at %d",
                      which, probe.after);
    }
}

ZTEST(service_priority, test_settings_are_the_kconfig_values)
{
    zassert_equal(CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY,
                  expected(CRUX_SERVICE_THREAD_SECURITY_EVENT_DRAIN));
    zassert_equal(CONFIG_CRUX_REBOOT_WORKER_PRIORITY, expected(CRUX_SERVICE_THREAD_REBOOT_WORKER));
    zassert_equal(CONFIG_CRUX_NETWORK_SETTINGS_WORKER_PRIORITY,
                  expected(CRUX_SERVICE_THREAD_NETWORK_SETTINGS_WORKER));
    zassert_equal(CONFIG_CRUX_WEB_SESSION_WORKER_PRIORITY,
                  expected(CRUX_SERVICE_THREAD_WEB_SESSION_WORKER));
    zassert_equal(CONFIG_CRUX_WEB_MANAGEMENT_WORKER_PRIORITY,
                  expected(CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER));
}

ZTEST(service_priority, test_unknown_identifier_changes_nothing)
{
    probe_t probe = run_probe(CRUX_THREAD_PRIORITY_LOW, CRUX_SERVICE_THREAD_COUNT, true);
    zassert_equal(probe.after, 6);
}

ZTEST_SUITE(service_priority, NULL, NULL, NULL, NULL, NULL);
