#ifndef CRUX_SERVICE_PRIORITY_PRIVATE_H
#define CRUX_SERVICE_PRIORITY_PRIVATE_H
/*
 * Private: places a Crux service worker at its per-service priority (ADR-0002 P3). Not a Thread
 * feature and not a consumer interface. See docs/thread-sync.md#priority-map-of-crux-threads.
 *
 * Each worker is a LOW-class Crux Thread. On Zephyr, where the Crux module defines
 * CRUX_SERVICE_PRIORITY_PLACEMENT, its entry calls crux_service_priority_place() first, and the
 * worker then runs at its CONFIG_CRUX_<SERVICE>_<ROLE>_PRIORITY. On the Linux Host nothing is
 * called, as Host Thread has no priorities.
 */
typedef enum {
    CRUX_SERVICE_THREAD_SECURITY_EVENT_DRAIN,
    CRUX_SERVICE_THREAD_REBOOT_WORKER,
    CRUX_SERVICE_THREAD_NETWORK_SETTINGS_WORKER,
    CRUX_SERVICE_THREAD_WEB_SESSION_WORKER,
    CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER,
    CRUX_SERVICE_THREAD_COUNT
} crux_service_thread_t;

/* Sets the calling thread's priority to the setting of `thread`. An unknown one changes nothing. */
void crux_service_priority_place(crux_service_thread_t thread);
#endif
