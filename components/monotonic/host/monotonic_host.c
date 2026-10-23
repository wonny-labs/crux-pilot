#include "monotonic_port.h"

#include <errno.h>
#include <stddef.h>
#include <time.h>

bool crux_monotonic_port_is_thread(void) {
    return true;
}

crux_status_t crux_monotonic_port_now(int64_t *out_ms) {
    struct timespec native;
    if (clock_gettime(CLOCK_MONOTONIC, &native) != 0) {
        return CRUX_ERR_IO;
    }
    if (native.tv_sec < 0) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    if (native.tv_nsec < 0 || native.tv_nsec >= 1000000000L) {
        return CRUX_ERR_IO;
    }
    int64_t fraction = native.tv_nsec / 1000000;
    if ((uintmax_t)native.tv_sec > (uintmax_t)(INT64_MAX / 1000) ||
        ((uintmax_t)native.tv_sec == (uintmax_t)(INT64_MAX / 1000) &&
         fraction > INT64_MAX % 1000)) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    *out_ms = (int64_t)native.tv_sec * 1000 + fraction;
    return CRUX_OK;
}

crux_status_t crux_monotonic_port_wait_until(int64_t deadline_ms) {
    int64_t seconds = deadline_ms / 1000;
    time_t native_seconds = (time_t)seconds;
    if ((uintmax_t)native_seconds != (uintmax_t)seconds) {
        return CRUX_ERR_OUT_OF_RANGE;
    }
    const struct timespec deadline = {.tv_sec = native_seconds,
                                      .tv_nsec = (long)(deadline_ms % 1000) * 1000000L};
    int result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, NULL);
    /* Interruption is an early wake; the common loop rechecks the same deadline. */
    return result == 0 || result == EINTR ? CRUX_OK : CRUX_ERR_IO;
}
