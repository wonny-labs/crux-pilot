#include "delay_port.h"
#include <crux/delay.h>
#include <stdio.h>

static int failures, queries, waits;
static int64_t limit = 100, waited;
static crux_status_t query_status, wait_status;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
crux_status_t crux_delay_port_get_max_ns(int64_t *out) {
    ++queries;
    *out = limit; /* Dirty native output even on error. */
    return query_status;
}
crux_status_t crux_delay_port_wait_ns(int64_t duration_ns) {
    ++waits;
    waited = duration_ns;
    return wait_status;
}
int main(void) {
    crux_duration_t out = crux_duration_from_ns(73);
    CHECK(crux_delay_get_max_duration(NULL) == CRUX_ERR_INVALID_ARGUMENT && queries == 0);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(0)) == CRUX_OK && queries == 0 && waits == 0);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(queries == 0 && waits == 0);
    CHECK(crux_delay_get_max_duration(&out) == CRUX_OK && crux_duration_to_ns(out) == 100);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(100)) == CRUX_OK && waits == 1 &&
          waited == 100);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(101)) == CRUX_ERR_OUT_OF_RANGE && waits == 1);
    query_status = CRUX_ERR_NOT_SUPPORTED;
    out = crux_duration_from_ns(73);
    CHECK(crux_delay_get_max_duration(&out) == CRUX_ERR_NOT_SUPPORTED &&
          crux_duration_to_ns(out) == 73);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_NOT_SUPPORTED && waits == 1);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(0)) == CRUX_OK && waits == 1);
    query_status = CRUX_ERR_IO;
    CHECK(crux_delay_get_max_duration(&out) == CRUX_ERR_IO && crux_duration_to_ns(out) == 73);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_IO && waits == 1);
    query_status = CRUX_OK;
    limit = -1;
    CHECK(crux_delay_get_max_duration(&out) == CRUX_ERR_IO && crux_duration_to_ns(out) == 73);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_IO && waits == 1);
    limit = 0;
    CHECK(crux_delay_get_max_duration(&out) == CRUX_OK && crux_duration_to_ns(out) == 0);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_OUT_OF_RANGE && waits == 1);
    limit = INT64_MAX;
    wait_status = CRUX_ERR_IO;
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(INT64_MAX)) == CRUX_ERR_IO && waits == 2);
    CHECK(waited == INT64_MAX);
    wait_status = CRUX_OK;
    CHECK(crux_delay_get_max_duration(&out) == CRUX_OK && crux_duration_to_ns(out) == INT64_MAX);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(INT64_MAX)) == CRUX_OK && waits == 3);
    printf("bounded Delay common contract: %d failures\n", failures);
    return failures != 0;
}
