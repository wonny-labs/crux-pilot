#define _POSIX_C_SOURCE 200809L
#include <crux/thread.h>
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
static struct timespec reading = {1, 0}, deadline_seen;
static int sleep_calls, read_calls, interrupt_once = 1, sleep_error, bad_call;
int __wrap_clock_gettime(clockid_t id, struct timespec *out) {
    if (id != CLOCK_MONOTONIC)
        ++bad_call;
    ++read_calls;
    *out = reading;
    return 0;
}
int __wrap_clock_getres(clockid_t id, struct timespec *out) {
    if (id != CLOCK_MONOTONIC)
        ++bad_call;
    *out = (struct timespec){0, 1};
    return 0;
}
int __wrap_clock_nanosleep(clockid_t id, int flags, const struct timespec *deadline,
                           struct timespec *remaining) {
    if (id != CLOCK_MONOTONIC || flags != TIMER_ABSTIME || remaining != NULL)
        ++bad_call;
    if (sleep_calls == 0)
        deadline_seen = *deadline;
    if (interrupt_once) {
        interrupt_once = 0;
        ++sleep_calls;
        return EINTR;
    }
    if (sleep_calls == 1 &&
        (deadline->tv_sec != deadline_seen.tv_sec || deadline->tv_nsec != deadline_seen.tv_nsec))
        ++bad_call;
    ++sleep_calls;
    reading = *deadline;
    return sleep_error;
}
int main(void) {
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(0)) == CRUX_OK);
    CHECK(read_calls == 0 && sleep_calls == 0);
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(8)) == CRUX_OK);
    CHECK(sleep_calls == 2 && deadline_seen.tv_sec == 1 && deadline_seen.tv_nsec == 9);
    CHECK(reading.tv_sec == 1 && reading.tv_nsec == 9);
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){0}) == CRUX_OK && sleep_calls == 2);
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){INT64_C(2147483648000000)}) == CRUX_OK);
    CHECK(reading.tv_sec == 2147483 && reading.tv_nsec == 648000000);
    sleep_error = EINVAL;
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){INT64_MAX}) == CRUX_ERR_IO);
    CHECK(reading.tv_sec == 9223372036LL && reading.tv_nsec == 854775807);
    CHECK(bad_call == 0);
    printf("typed Thread Linux sleep: %d failures\n", failures);
    return failures != 0;
}
