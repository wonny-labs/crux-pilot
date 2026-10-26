#include "time_clock_port.h"
#include <limits.h>
#include <stdio.h>

static int failures;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
static bool thread_context = true;
static int reads, upper_reads;
static int64_t now_ns = 100, upper_ns = 110;
static crux_status_t read_status = CRUX_OK;
bool crux_time_clock_is_thread(void)
{
    return thread_context;
}
crux_status_t crux_time_clock_read(int64_t *out)
{
    ++reads;
    *out = now_ns; /* Dirty even on error, to exercise public output preservation. */
    return read_status;
}
crux_status_t crux_time_clock_read_upper_bound(int64_t *out)
{
    ++upper_reads;
    *out = upper_ns;
    return read_status;
}
int main(void)
{
    crux_monotonic_time_t out = {73};
    CHECK(crux_time_get_monotonic(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_get_monotonic(&out) == CRUX_OK && out.private_ns == 100);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(0), &out) == CRUX_OK);
    CHECK(out.private_ns == 100 && upper_reads == 0);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(5), &out) == CRUX_OK);
    CHECK(out.private_ns == 115 && upper_reads == 1);
    int old_reads = reads, old_upper = upper_reads;
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(-1), &out) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), NULL) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(reads == old_reads && upper_reads == old_upper && out.private_ns == 115);
    read_status = CRUX_ERR_IO;
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_IO && out.private_ns == 115);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_IO &&
          out.private_ns == 115);
    read_status = CRUX_OK;
    now_ns = -1;
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_OUT_OF_RANGE && out.private_ns == 115);
    upper_ns = -1;
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_OUT_OF_RANGE &&
          out.private_ns == 115);
    upper_ns = INT64_MAX;
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(1), &out) == CRUX_ERR_OUT_OF_RANGE &&
          out.private_ns == 115);
    now_ns = INT64_MAX;
    CHECK(crux_time_get_monotonic(&out) == CRUX_OK && out.private_ns == INT64_MAX);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(0), &out) == CRUX_OK &&
          out.private_ns == INT64_MAX);
    thread_context = false;
    old_reads = reads;
    old_upper = upper_reads;
    CHECK(crux_time_get_monotonic(&out) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(0), &out) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(crux_time_make_deadline_after(crux_duration_from_ns(-1), &out) ==
          CRUX_ERR_INVALID_ARGUMENT);
    CHECK(reads == old_reads && upper_reads == old_upper && out.private_ns == INT64_MAX);

    int64_t ns = 73;
    CHECK(crux_time_ticks_to_ns(127, 128, false, &ns) == CRUX_OK && ns == 992187500);
    CHECK(crux_time_ticks_to_ns(1, 3, false, &ns) == CRUX_OK && ns == 333333333);
    CHECK(crux_time_ticks_to_ns(1, 3, true, &ns) == CRUX_OK && ns == 333333334);
    CHECK(crux_time_ticks_to_ns(128, 128, true, &ns) == CRUX_OK && ns == 1000000000);
    ns = 73;
    CHECK(crux_time_ticks_to_ns(INT64_MAX, 1, false, &ns) == CRUX_ERR_OUT_OF_RANGE && ns == 73);
    CHECK(crux_time_ticks_to_ns(-1, 128, false, &ns) == CRUX_ERR_OUT_OF_RANGE && ns == 73);
    CHECK(crux_time_ticks_to_ns(1, 0, false, &ns) == CRUX_ERR_OUT_OF_RANGE && ns == 73);
    CHECK(crux_time_ticks_to_ns(0, 1, false, NULL) == CRUX_ERR_INVALID_ARGUMENT);
    printf("typed Time clock: %d failures\n", failures);
    return failures != 0;
}
