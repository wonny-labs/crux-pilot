#include "service_priority.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/*
 * Kconfig's ranges stop at CONFIG_NUM_PREEMPT_PRIORITIES, which is the idle thread's priority, as
 * Kconfig cannot subtract. These refuse it, so that a worker is a preemptible application thread.
 */
#define CRUX_SERVICE_PRIORITY_CHECK(value)                                                         \
    BUILD_ASSERT((value) >= 0 && (value) < CONFIG_NUM_PREEMPT_PRIORITIES,                          \
                 #value " must be a preemptible application priority, 0 to "                       \
                        "CONFIG_NUM_PREEMPT_PRIORITIES - 1")

CRUX_SERVICE_PRIORITY_CHECK(CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY);
CRUX_SERVICE_PRIORITY_CHECK(CONFIG_CRUX_REBOOT_WORKER_PRIORITY);
CRUX_SERVICE_PRIORITY_CHECK(CONFIG_CRUX_NETWORK_SETTINGS_WORKER_PRIORITY);
CRUX_SERVICE_PRIORITY_CHECK(CONFIG_CRUX_WEB_SESSION_WORKER_PRIORITY);
CRUX_SERVICE_PRIORITY_CHECK(CONFIG_CRUX_WEB_MANAGEMENT_WORKER_PRIORITY);

static const int priorities[] = {
    [CRUX_SERVICE_THREAD_SECURITY_EVENT_DRAIN] = CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY,
    [CRUX_SERVICE_THREAD_REBOOT_WORKER] = CONFIG_CRUX_REBOOT_WORKER_PRIORITY,
    [CRUX_SERVICE_THREAD_NETWORK_SETTINGS_WORKER] = CONFIG_CRUX_NETWORK_SETTINGS_WORKER_PRIORITY,
    [CRUX_SERVICE_THREAD_WEB_SESSION_WORKER] = CONFIG_CRUX_WEB_SESSION_WORKER_PRIORITY,
    [CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER] = CONFIG_CRUX_WEB_MANAGEMENT_WORKER_PRIORITY,
};
BUILD_ASSERT(ARRAY_SIZE(priorities) == CRUX_SERVICE_THREAD_COUNT, "a worker has no setting");

void crux_service_priority_place(crux_service_thread_t thread)
{
    if ((unsigned)thread >= ARRAY_SIZE(priorities)) {
        return;
    }
    /* At the default the worker already runs at its class's number, and nothing changes. */
    k_tid_t self = k_current_get();
    if (k_thread_priority_get(self) != priorities[thread]) {
        k_thread_priority_set(self, priorities[thread]);
    }
}
