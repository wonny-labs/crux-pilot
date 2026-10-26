#include <crux/delay.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int failures, reads, res_reads, forbidden_calls;
static int get_error, res_error, error_at_read, malformed;
static int64_t now_ns = 100, resolution_ns = 10;
static int64_t script[16];
static size_t script_count, script_index;
static bool active;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int __wrap_clock_gettime(clockid_t clock, struct timespec *out)
{
    CHECK(clock == CLOCK_MONOTONIC);
    ++reads;
    if (get_error || reads == error_at_read || reads > 32) {
        errno = get_error ? get_error : EIO;
        return -1;
    }
    int64_t value = script_index < script_count ? script[script_index++] : now_ns;
    *out = (struct timespec){value / INT64_C(1000000000), value % INT64_C(1000000000)};
    if (malformed == 1)
        out->tv_nsec = -1;
    if (malformed == 2)
        out->tv_sec = INT64_MAX / INT64_C(1000000000) + 1;
    if (malformed == 3)
        out->tv_sec = -1;
    return 0;
}
int __wrap_clock_getres(clockid_t clock, struct timespec *out)
{
    CHECK(clock == CLOCK_MONOTONIC);
    ++res_reads;
    *out =
        (struct timespec){resolution_ns / INT64_C(1000000000), resolution_ns % INT64_C(1000000000)};
    if (res_error) {
        errno = res_error;
        return -1;
    }
    return 0;
}
/* Only calls made while executing the Delay API are prohibited. */
void *__real_malloc(size_t size);
void *__wrap_malloc(size_t size)
{
    if (active) {
        ++forbidden_calls;
        return NULL;
    }
    return __real_malloc(size);
}
int __real_pthread_mutex_lock(pthread_mutex_t *mutex);
int __wrap_pthread_mutex_lock(pthread_mutex_t *mutex)
{
    if (active) {
        ++forbidden_calls;
        return EINVAL;
    }
    return __real_pthread_mutex_lock(mutex);
}
int __wrap_nanosleep(const struct timespec *request, struct timespec *remaining)
{
    (void)request;
    (void)remaining;
    ++forbidden_calls;
    errno = EINTR;
    return -1;
}
int __wrap_clock_nanosleep(clockid_t clock, int flags, const struct timespec *request,
                           struct timespec *remaining)
{
    (void)clock;
    (void)flags;
    (void)request;
    (void)remaining;
    ++forbidden_calls;
    return EINTR;
}
int __wrap_sched_yield(void)
{
    ++forbidden_calls;
    return 0;
}
static crux_status_t wait_ns(int64_t ns)
{
    active = true;
    crux_status_t status = crux_delay_busy_wait(crux_duration_from_ns(ns));
    active = false;
    CHECK(forbidden_calls == 0);
    return status;
}
static crux_status_t limit(crux_duration_t *out)
{
    active = true;
    crux_status_t status = crux_delay_get_max_duration(out);
    active = false;
    CHECK(forbidden_calls == 0);
    return status;
}
static void reset(void)
{
    reads = res_reads = get_error = res_error = error_at_read = malformed = 0;
    now_ns = 100;
    resolution_ns = 10;
    script_count = script_index = 0;
}
int main(void)
{
    crux_duration_t out = crux_duration_from_ns(73);
    CHECK(limit(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(wait_ns(0) == CRUX_OK && reads == 0 && res_reads == 0);
    CHECK(wait_ns(-1) == CRUX_ERR_INVALID_ARGUMENT && reads == 0 && res_reads == 0);
    CHECK(limit(&out) == CRUX_OK && crux_duration_to_ns(out) == INT64_MAX - 110);
    reset();
    CHECK(wait_ns(INT64_MAX - 109) == CRUX_ERR_OUT_OF_RANGE && reads == 1);
    reset();
    const int64_t sequence[] = {100, 100, 110, 119, 120};
    for (size_t i = 0; i < 5; ++i)
        script[i] = sequence[i];
    script_count = 5;
    CHECK(wait_ns(10) == CRUX_OK && reads == 5 && script_index == 5);
    reset();
    resolution_ns = 1;
    script[0] = script[1] = INT64_MAX - 10;
    script[2] = INT64_MAX - 2;
    script[3] = INT64_MAX - 1;
    script_count = 4;
    CHECK(wait_ns(8) == CRUX_OK && reads == 4);
    reset();
    script[0] = INT64_MAX - 100;
    script[1] = INT64_MAX - 20;
    script_count = 2;
    CHECK(wait_ns(50) == CRUX_ERR_OUT_OF_RANGE && reads == 2);
    reset();
    now_ns = INT64_MAX - 10;
    CHECK(limit(&out) == CRUX_OK && crux_duration_to_ns(out) == 0);
    CHECK(wait_ns(1) == CRUX_ERR_OUT_OF_RANGE);
    now_ns = INT64_MAX;
    out = crux_duration_from_ns(73);
    CHECK(limit(&out) == CRUX_ERR_OUT_OF_RANGE && crux_duration_to_ns(out) == 73);
    reset();
    get_error = EIO;
    CHECK(limit(&out) == CRUX_ERR_IO && crux_duration_to_ns(out) == 73);
    reset();
    res_error = EIO;
    CHECK(limit(&out) == CRUX_ERR_IO && crux_duration_to_ns(out) == 73);
    reset();
    get_error = ENOSYS;
    CHECK(limit(&out) == CRUX_ERR_NOT_SUPPORTED && crux_duration_to_ns(out) == 73);
    reset();
    res_error = ENOTSUP;
    CHECK(limit(&out) == CRUX_ERR_NOT_SUPPORTED && crux_duration_to_ns(out) == 73);
    reset();
    resolution_ns = 0;
    CHECK(limit(&out) == CRUX_ERR_IO && crux_duration_to_ns(out) == 73);
    for (malformed = 1; malformed <= 3; ++malformed) {
        now_ns = 100;
        resolution_ns = 10;
        CHECK(limit(&out) == (malformed == 1 ? CRUX_ERR_IO : CRUX_ERR_OUT_OF_RANGE));
        CHECK(crux_duration_to_ns(out) == 73);
    }
    reset();
    script[0] = script[1] = 100;
    script_count = 2;
    error_at_read = 3;
    CHECK(wait_ns(10) == CRUX_ERR_IO);
    printf("Linux bounded CPU wait/resolution/headroom: %d failures\n", failures);
    return failures != 0;
}
