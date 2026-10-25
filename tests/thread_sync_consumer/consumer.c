#if CRUX_TEST_THREAD
#include <crux/thread.h>
#if __has_include(<crux/sync.h>)
#error "Sync public header leaked into Thread-only consumer"
#endif
#else
#include <crux/sync.h>
#if __has_include(<crux/thread.h>)
#error "Thread public header leaked into Sync-only consumer"
#endif
#endif

#if __has_include("thread_internal.h") ||                                                          \
                  __has_include("sync_internal.h") ||                                              \
                                __has_include("thread_native_types.h") ||                          \
                                              __has_include("sync_native_types.h")
#error "Private component headers leaked into public consumer"
#endif

#if CRUX_TEST_THREAD
static void worker(void *argument) {
    *(int *)argument = 42;
}
int component_consumer_run(void) {
    int observed = 0;
    crux_thread_t thread;
    const crux_thread_options_t options = {.priority = CRUX_THREAD_PRIORITY_NORMAL};
    if (crux_thread_create(&options, worker, &observed, &thread) != CRUX_OK ||
        crux_thread_join(&thread, 1000) != CRUX_OK || observed != 42 || thread.slot != 0) {
        return 1;
    }
    return crux_thread_sleep_for(crux_duration_from_ms(1)) != CRUX_OK;
}
#else
int component_native_sync_worker(crux_mutex_t mutex);
int component_consumer_run(void) {
    crux_mutex_t mutex;
    crux_semaphore_t semaphore;
    if (crux_mutex_create(&mutex) != CRUX_OK || component_native_sync_worker(mutex) != 0 ||
        crux_mutex_lock(mutex, 0) != CRUX_OK || crux_mutex_unlock(mutex) != CRUX_OK ||
        crux_mutex_destroy(&mutex) != CRUX_OK ||
        crux_semaphore_create(1, 1, &semaphore) != CRUX_OK ||
        crux_semaphore_give(semaphore) != CRUX_ERR_BUSY ||
        crux_semaphore_take(semaphore, 0) != CRUX_OK ||
        crux_semaphore_take(semaphore, 0) != CRUX_ERR_BUSY ||
        crux_semaphore_destroy(&semaphore) != CRUX_OK) {
        return 1;
    }
    return 0;
}
#endif
