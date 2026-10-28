#include "busy_wait_port.h"
#include <crux/time.h>
#include <stdio.h>

static int failures, waits;
static int64_t limit = 100, waited;
static crux_status_t wait_status;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
crux_status_t crux_busy_wait_ns(int64_t duration_ns)
{
    ++waits;
    waited = duration_ns;
    return duration_ns > limit ? CRUX_ERR_OUT_OF_RANGE : wait_status;
}
int main(void)
{
    CHECK(crux_busy_wait(crux_duration_from_ns(0)) == CRUX_OK && waits == 0);
    CHECK(crux_busy_wait(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT && waits == 0);
    CHECK(crux_busy_wait(crux_duration_from_ns(100)) == CRUX_OK && waited == 100);
    CHECK(crux_busy_wait(crux_duration_from_ns(101)) == CRUX_ERR_OUT_OF_RANGE);
    wait_status = CRUX_ERR_NOT_SUPPORTED;
    CHECK(crux_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_NOT_SUPPORTED);
    wait_status = CRUX_ERR_IO;
    CHECK(crux_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_IO);
    int before = waits;
    CHECK(crux_busy_wait(crux_duration_from_ns(0)) == CRUX_OK && waits == before);
    limit = INT64_MAX;
    wait_status = CRUX_OK;
    CHECK(crux_busy_wait(crux_duration_from_ns(INT64_MAX)) == CRUX_OK && waited == INT64_MAX);
    printf("Time Busy-wait common contract: %d failures\n", failures);
    return failures != 0;
}
