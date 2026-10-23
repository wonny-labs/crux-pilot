#include "monotonic_port.h"
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
static int64_t now_value = 100;
static crux_status_t read_status = CRUX_OK;
static crux_status_t wait_status = CRUX_OK;
static int reads, waits;
static int64_t wait_deadlines[4];
bool crux_monotonic_port_is_thread(void) {
    return thread_context;
}
crux_status_t crux_monotonic_port_now(int64_t *out) {
    ++reads;
    /* Deliberately dirty the private output even on error. Public output must survive. */
    *out = now_value;
    return read_status;
}
crux_status_t crux_monotonic_port_wait_until(int64_t deadline) {
    if (waits < 4)
        wait_deadlines[waits] = deadline;
    ++waits;
    now_value += 1; /* Early wakes before the absolute deadline. */
    return wait_status;
}
int main(void) {
    int64_t out = 73;
    CHECK(crux_monotonic_now(NULL) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_monotonic_now(&out) == CRUX_OK && out == 100);
    read_status = CRUX_ERR_IO;
    out = 73;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_IO && out == 73);
    read_status = CRUX_ERR_OUT_OF_RANGE;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    read_status = CRUX_OK;
    now_value = -1;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    now_value = 100;
    CHECK(crux_monotonic_sleep_until(-1) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(crux_monotonic_sleep_until(0) == CRUX_OK && waits == 0);
    CHECK(crux_monotonic_sleep_until(100) == CRUX_OK && waits == 0);
    CHECK(crux_monotonic_sleep_until(103) == CRUX_OK && now_value == 103 && waits == 3);
    CHECK(wait_deadlines[0] == 103 && wait_deadlines[1] == 103 && wait_deadlines[2] == 103);
    int old_waits = waits;
    CHECK(crux_monotonic_sleep_until(103 + (int64_t)INT32_MAX + 1) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(waits == old_waits);
    wait_status = CRUX_ERR_IO;
    CHECK(crux_monotonic_sleep_until(103 + (int64_t)INT32_MAX) == CRUX_ERR_IO);
    CHECK(waits == old_waits + 1);
    wait_status = CRUX_OK;
    read_status = CRUX_ERR_IO;
    CHECK(crux_monotonic_sleep_until(104) == CRUX_ERR_IO);
    CHECK(waits == old_waits + 1);
    thread_context = false;
    out = 73;
    int old_reads = reads;
    CHECK(crux_monotonic_now(&out) == CRUX_ERR_NOT_SUPPORTED && out == 73);
    CHECK(crux_monotonic_sleep_until(0) == CRUX_ERR_NOT_SUPPORTED);
    CHECK(reads == old_reads && waits == old_waits + 1);

    /* Literal fractional, exact and overflow boundaries catch truncation/overflow. */
    CHECK(crux_monotonic_ticks_to_ms(127, 128, &out) == CRUX_OK && out == 992);
    CHECK(crux_monotonic_ticks_to_ms(128, 128, &out) == CRUX_OK && out == 1000);
    CHECK(crux_monotonic_ticks_to_ms(INT64_MAX, 1000, &out) == CRUX_OK && out == INT64_MAX);
    out = 73;
    CHECK(crux_monotonic_ticks_to_ms(INT64_MAX, 1, &out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    CHECK(crux_monotonic_ticks_to_ms(-1, 1000, &out) == CRUX_ERR_OUT_OF_RANGE && out == 73);
    CHECK(crux_monotonic_ms_to_ticks_ceil(1, 128, &out) == CRUX_OK && out == 1);
    CHECK(crux_monotonic_ms_to_ticks_ceil(125, 128, &out) == CRUX_OK && out == 16);
    CHECK(crux_monotonic_ms_to_ticks_ceil(126, 128, &out) == CRUX_OK && out == 17);
    CHECK(crux_monotonic_ms_to_ticks_ceil(INT64_MAX, 1000, &out) == CRUX_OK && out == INT64_MAX);
    out = 73;
    CHECK(crux_monotonic_ms_to_ticks_ceil(INT64_MAX, 1001, &out) == CRUX_ERR_OUT_OF_RANGE &&
          out == 73);
    printf("monotonic common: %d failures\n", failures);
    return failures != 0;
}
