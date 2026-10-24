#include <crux/status.h>
#if PROFILE_TIME
#include <crux/time.h>
#elif PROFILE_THREAD
#include <crux/thread.h>
#elif PROFILE_SYNC
#include <crux/sync.h>
#elif PROFILE_SERIAL
#include <crux/serial.h>
#elif PROFILE_LOGGING
#include <crux/logging.h>
#elif PROFILE_MESSAGING
#include <crux/monotonic.h>
#include <crux/queue.h>
#endif
static_assert(__cplusplus >= 201703L, "C++17 consumer");
extern "C" int external_cpp_consumer(void) {
#if PROFILE_TIME
    return crux_time_get(nullptr) != CRUX_ERR_INVALID_ARGUMENT;
#elif PROFILE_THREAD
    return crux_thread_join(nullptr, 0) != CRUX_ERR_INVALID_ARGUMENT;
#elif PROFILE_SYNC
    return crux_mutex_destroy(nullptr) != CRUX_ERR_INVALID_ARGUMENT;
#elif PROFILE_SERIAL
    return crux_serial_close(nullptr) != CRUX_ERR_INVALID_ARGUMENT;
#elif PROFILE_LOGGING
    return crux_log_write(CRUX_LOG_INFO, nullptr, "probe") != CRUX_ERR_INVALID_ARGUMENT;
#elif PROFILE_MESSAGING
    return crux_queue_create(1, sizeof(int), nullptr) != CRUX_ERR_INVALID_ARGUMENT ||
           crux_monotonic_now(nullptr) != CRUX_ERR_INVALID_ARGUMENT;
#else
    return CRUX_OK != 0;
#endif
}
