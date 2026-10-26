#if CRUX_TEST_THREAD
#include <crux/thread.h>
#if __has_include(<crux/sync.h>)
#error "Sync public header leaked into Thread-only consumer"
#endif
static_assert(sizeof(crux_thread_t) == 2 * sizeof(uint32_t));
#else
#include <crux/sync.h>
#if __has_include(<crux/thread.h>)
#error "Thread public header leaked into Sync-only consumer"
#endif
static_assert(sizeof(crux_mutex_t) == 2 * sizeof(uint32_t));
static_assert(sizeof(crux_semaphore_t) == 2 * sizeof(uint32_t));
#endif

extern "C" int component_consumer_run(void);
extern "C" int component_cpp_consumer_run(void)
{
#if CRUX_TEST_THREAD
    if (crux_thread_join(nullptr, 0) != CRUX_ERR_INVALID_ARGUMENT) {
        return 2;
    }
    if (crux_thread_sleep_for(crux_duration_from_ns(-1)) != CRUX_ERR_INVALID_ARGUMENT) {
        return 3;
    }
#else
    if (crux_mutex_destroy(nullptr) != CRUX_ERR_INVALID_ARGUMENT) {
        return 2;
    }
#endif
    return component_consumer_run();
}
