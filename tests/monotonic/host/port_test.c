#define _POSIX_C_SOURCE 200809L
#include <crux/monotonic.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
static struct timespec reading;
static int read_result, sleep_result, sleep_calls;
static struct timespec slept_to[4];
static int clock_modes_ok = 1;
int __wrap_clock_gettime(clockid_t clock_id, struct timespec *out) {
    if (clock_id != CLOCK_MONOTONIC)
        clock_modes_ok = 0;
    *out = reading;
    return read_result;
}
int __wrap_clock_nanosleep(clockid_t clock_id, int flags, const struct timespec *deadline,
                           struct timespec *remaining) {
    if (clock_id != CLOCK_MONOTONIC || flags != TIMER_ABSTIME || remaining != NULL)
        clock_modes_ok = 0;
    if (sleep_calls < 4)
        slept_to[sleep_calls] = *deadline;
    ++sleep_calls;
    if (sleep_result == EINTR) {
        sleep_result = 0;
        return EINTR;
    }
    reading = *deadline;
    return sleep_result;
}
int main(void) {
    int64_t out = 73;
    reading = (struct timespec){.tv_sec = 4, .tv_nsec = 999999999};
    CHECK(crux_monotonic_now(&out) == CRUX_OK && out == 4999);
    reading.tv_sec = 9223372036854775LL;
    reading.tv_nsec = 807999999;
    CHECK(crux_monotonic_now(&out) == CRUX_OK && out == INT64_MAX);
    out = 73;
    reading.tv_nsec = 808000000;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    reading.tv_sec = 9223372036854776LL;
    reading.tv_nsec = 0;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    reading.tv_sec = -1;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    reading.tv_sec = 1;
    reading.tv_nsec = 1000000000;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_IO && out == 73);
    reading.tv_nsec = -1;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_IO && out == 73);
    read_result = -1;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_IO && out == 73);
    read_result = 0;
    reading = (struct timespec){.tv_sec = 1, .tv_nsec = 0};
    sleep_result = EINTR;
    CHECK(crux_monotonic_sleep_until(1001) == CRUX_OK && sleep_calls == 2);
    CHECK(slept_to[0].tv_sec == 1 && slept_to[0].tv_nsec == 1000000);
    CHECK(slept_to[1].tv_sec == 1 && slept_to[1].tv_nsec == 1000000);
    sleep_result = EINVAL;
    CHECK(crux_monotonic_sleep_until(1002) == CRUX_ERR_IO);
    CHECK(clock_modes_ok);
    printf("monotonic native boundaries: %d failures\n", failures);
    return failures != 0;
}
