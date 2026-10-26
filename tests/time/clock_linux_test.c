#define _POSIX_C_SOURCE 200809L
#include <crux/time.h>
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
static struct timespec reading = {4, 999999999}, resolution = {0, 10};
static int read_error, resolution_error, resolution_calls, wrong_clock;
int __wrap_clock_gettime(clockid_t id, struct timespec *out)
{
    if (id != CLOCK_MONOTONIC)
        ++wrong_clock;
    *out = reading;
    return read_error;
}
int __wrap_clock_getres(clockid_t id, struct timespec *out)
{
    if (id != CLOCK_MONOTONIC)
        ++wrong_clock;
    ++resolution_calls;
    *out = resolution;
    return resolution_error;
}
int main(void)
{
    crux_monotonic_time_t out = {73};
    CHECK(crux_time_get_monotonic(&out) == CRUX_OK && out.private_ns == INT64_C(4999999999));
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(0), &out) == CRUX_OK);
    CHECK(out.private_ns == INT64_C(4999999999) && resolution_calls == 0);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_OK);
    CHECK(out.private_ns == INT64_C(5000000010));
    reading = (struct timespec){9223372036LL, 854775807};
    CHECK(crux_time_get_monotonic(&out) == CRUX_OK && out.private_ns == INT64_MAX);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(out.private_ns == INT64_MAX);
    ++reading.tv_nsec;
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_OUT_OF_RANGE && out.private_ns == INT64_MAX);
    reading = (struct timespec){-1, 0};
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_OUT_OF_RANGE);
    reading = (struct timespec){1, -1};
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_IO);
    reading.tv_nsec = 1000000000;
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_IO);
    reading.tv_nsec = 0;
    read_error = -1;
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_IO);
    read_error = 0;
    resolution_error = -1;
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_IO);
    resolution_error = 0;
    resolution.tv_nsec = 0;
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_IO);
    resolution.tv_nsec = -1;
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_IO);
    CHECK(out.private_ns == INT64_MAX && wrong_clock == 0);
    printf("typed Time Linux clock: %d failures\n", failures);
    return failures != 0;
}
