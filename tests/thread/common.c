#include "monotonic_port.h"
#include "thread_sleep_port.h"
#include <stdio.h>

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
static _Thread_local bool context = true;
static int64_t now_ns = 100, upper_ns = 110, original_deadline_ns;
static int reads, upper_reads, waits, bad_deadlines;
static crux_status_t read_status = CRUX_OK, wait_status = CRUX_OK;
bool crux_is_in_isr(void)
{
    return !context;
}
crux_status_t crux_monotonic_impl_read_ns(int64_t *out)
{
    ++reads;
    *out = now_ns;
    return read_status;
}
crux_status_t crux_monotonic_impl_read_upper_ns(int64_t *out)
{
    ++upper_reads;
    *out = upper_ns;
    return read_status;
}
crux_status_t crux_thread_impl_sleep_until_ns(int64_t deadline_ns)
{
    if (deadline_ns != original_deadline_ns)
        ++bad_deadlines;
    ++waits;
    now_ns += 1;
    return wait_status;
}
int main(void)
{
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(0)) == CRUX_OK);
    CHECK(reads == 0 && upper_reads == 0 && waits == 0);
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT);
    int64_t ticks = 73;
    CHECK(crux_thread_ns_to_ticks_ceil(1, 128, &ticks) == CRUX_OK && ticks == 1);
    CHECK(crux_thread_ns_to_ticks_ceil(125000000, 128, &ticks) == CRUX_OK && ticks == 16);
    CHECK(crux_thread_ns_to_ticks_ceil(125000001, 128, &ticks) == CRUX_OK && ticks == 17);
    ticks = 73;
    CHECK(crux_thread_ns_to_ticks_ceil(INT64_MAX, 1000000000, &ticks) == CRUX_ERR_OUT_OF_RANGE &&
          ticks == 73);
    CHECK(crux_thread_ns_to_ticks_ceil(INT64_MAX - 1, 1000000000, &ticks) == CRUX_OK &&
          ticks == INT64_MAX - 1);
    CHECK(crux_thread_ns_to_ticks_ceil(INT64_MAX, 1000000001, &ticks) == CRUX_ERR_OUT_OF_RANGE &&
          ticks == INT64_MAX - 1);
    CHECK(crux_thread_ns_to_ticks_ceil(-1, 128, &ticks) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_thread_ns_to_ticks_ceil(1, 0, &ticks) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(crux_thread_ns_to_ticks_ceil(0, 128, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){-1}) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){99}) == CRUX_OK && waits == 0);
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){100}) == CRUX_OK && waits == 0);
    original_deadline_ns = 103;
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){103}) == CRUX_OK);
    CHECK(waits == 3 && now_ns == 103 && bad_deadlines == 0);
    now_ns = 110;
    upper_ns = 112;
    original_deadline_ns = 115;
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(3)) == CRUX_OK);
    CHECK(now_ns == 115 && upper_reads == 1 && bad_deadlines == 0);
    wait_status = CRUX_ERR_IO;
    original_deadline_ns = INT64_C(2147483648000000);
    int old_waits = waits;
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){original_deadline_ns}) == CRUX_ERR_IO);
    CHECK(waits == old_waits + 1); /* Accepted beyond the former INT32_MAX ms cap. */
    read_status = CRUX_ERR_IO;
    old_waits = waits;
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){1000}) == CRUX_ERR_IO);
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(1)) == CRUX_ERR_IO && waits == old_waits);
    context = false;
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(0)) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_thread_sleep_until((crux_monotonic_time_t){0}) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_thread_sleep_for(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT);
    printf("typed Thread sleep: %d failures\n", failures);
    return failures != 0;
}
