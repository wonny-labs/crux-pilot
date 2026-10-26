#include <crux/time_authority.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static int failures, get_calls, set_calls;
static int get_error, set_error;
static struct timespec native_time = {1700000000, 123456789};
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
/* All native writes are intercepted. There is no __real_clock_settime path. */
int __wrap_clock_gettime(clockid_t clock, struct timespec *out)
{
    CHECK(clock == CLOCK_REALTIME);
    ++get_calls;
    *out = native_time;
    if (get_error) {
        errno = get_error;
        return -1;
    }
    return 0;
}
int __wrap_clock_settime(clockid_t clock, const struct timespec *value)
{
    CHECK(clock == CLOCK_REALTIME);
    ++set_calls;
    native_time = *value;
    if (set_error) {
        errno = set_error;
        return -1;
    }
    return 0;
}
int main(void)
{
    CHECK(crux_time_init() == CRUX_OK && get_calls == 0 && set_calls == 0);
    crux_realtime_t out = {42, 73};
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_NO_DATA && get_calls == 0);
    CHECK(crux_time_adopt_realtime() == CRUX_OK && set_calls == 0);
    CHECK(crux_time_get_realtime(&out) == CRUX_OK && out.seconds == 1700000000 &&
          out.nanoseconds == 123456789);
    crux_realtime_t value = {CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE - 1, 999999999};
    CHECK(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_EXTERNAL, true) == CRUX_OK);
    CHECK(native_time.tv_sec == value.seconds && native_time.tv_nsec == value.nanoseconds);
    crux_realtime_snapshot_t s;
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_OK && s.is_actively_synchronized);
    get_error = EIO;
    out = (crux_realtime_t){42, 73};
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_IO && out.seconds == 42 &&
          out.nanoseconds == 73);
    get_error = 0;
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_OK && s.is_actively_synchronized);
    native_time.tv_nsec = -1;
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_IO && out.seconds == 42);
    native_time.tv_nsec = 1000000000;
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_IO && out.seconds == 42);
    native_time.tv_nsec = 0;
    native_time.tv_sec = CRUX_TIME_MAX_UNIX_SECONDS_EXCLUSIVE;
    CHECK(crux_time_get_realtime(&out) == CRUX_ERR_OUT_OF_RANGE && out.seconds == 42);
    int before = get_calls;
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_OK && !s.is_time_valid &&
          !s.is_actively_synchronized && s.last_source == CRUX_TIME_SOURCE_EXTERNAL &&
          get_calls == before);
    CHECK(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_MANUAL, false) == CRUX_OK);
    set_error = EPERM;
    CHECK(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_RTC, false) == CRUX_ERR_IO);
    CHECK(crux_time_get_realtime_snapshot(&s) == CRUX_OK && !s.is_time_valid &&
          s.last_source == CRUX_TIME_SOURCE_MANUAL);
    set_error = 0;
    CHECK(crux_time_set_realtime(&value, CRUX_TIME_SOURCE_RTC, false) == CRUX_OK);
    CHECK(crux_time_get_realtime(&out) == CRUX_OK && out.nanoseconds == 999999999);
    printf("wrapped Linux realtime (no system clock writes): %d failures\n", failures);
    return failures != 0;
}
