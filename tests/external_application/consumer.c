#include <crux/status.h>
#if PROFILE_TIME
#include <crux/time.h>
#elif PROFILE_THREAD
#include <crux/thread.h>
static void worker(void *argument) {
    *(int *)argument = 42;
}
#elif PROFILE_SYNC
#include <crux/sync.h>
#elif PROFILE_SERIAL
#include <crux/serial.h>
#elif PROFILE_LOGGING
#include <crux/logging.h>
#elif PROFILE_SYSTEM
#include <crux/system.h>
#elif PROFILE_MESSAGING
#include <crux/monotonic.h>
#include <crux/queue.h>
#endif
int external_consumer(void) {
#if PROFILE_TIME
    crux_time_instant_t now = 0;
    return crux_time_init() != CRUX_OK || crux_time_set(CRUX_TIME_MIN_UNIX_SECONDS) != CRUX_OK ||
           crux_time_get(&now) != CRUX_OK || now != CRUX_TIME_MIN_UNIX_SECONDS;
#elif PROFILE_THREAD
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    crux_thread_t thread;
    int observed = 0;
    return crux_thread_create(&options, worker, &observed, &thread) != CRUX_OK ||
           crux_thread_join(&thread, 1000) != CRUX_OK || observed != 42 ||
           crux_thread_sleep_for(crux_duration_from_ms(1)) != CRUX_OK;
#elif PROFILE_SYNC
    crux_mutex_t mutex;
    crux_semaphore_t semaphore;
    return crux_mutex_create(&mutex) != CRUX_OK || crux_mutex_lock(mutex, 0) != CRUX_OK ||
           crux_mutex_unlock(mutex) != CRUX_OK || crux_mutex_destroy(&mutex) != CRUX_OK ||
           crux_semaphore_create(1, 1, &semaphore) != CRUX_OK ||
           crux_semaphore_take(semaphore, 0) != CRUX_OK ||
           crux_semaphore_take(semaphore, 0) != CRUX_ERR_BUSY ||
           crux_semaphore_destroy(&semaphore) != CRUX_OK;
#elif PROFILE_SERIAL
    crux_serial_t handle = {1, 1};
    return crux_serial_open(0, NULL, &handle) != CRUX_ERR_INVALID_ARGUMENT || handle.slot != 0 ||
           handle.generation != 0;
#elif PROFILE_LOGGING
    return crux_log_set_level(CRUX_LOG_INFO) != CRUX_OK ||
           crux_log_write(CRUX_LOG_INFO, "external", "logging") != CRUX_OK;
#elif PROFILE_SYSTEM
    crux_system_status_t report;
    return crux_system_status_get(&report) != CRUX_OK || !report.watchdog_emulated ||
           report.reset_status != CRUX_ERR_NOT_SUPPORTED;
#elif PROFILE_MESSAGING
    crux_queue_t queue;
    int sent = 42;
    int received = 0;
    int64_t now = -1;
    return crux_queue_create(1, sizeof(sent), &queue) != CRUX_OK ||
           crux_queue_send(queue, &sent, sizeof(sent), 0) != CRUX_OK ||
           crux_queue_receive(queue, &received, sizeof(received), 0) != CRUX_OK ||
           received != sent || crux_queue_destroy(&queue) != CRUX_OK ||
           crux_monotonic_now(&now) != CRUX_OK || crux_monotonic_sleep_until(now) != CRUX_OK;
#else
    return CRUX_OK != 0;
#endif
}
