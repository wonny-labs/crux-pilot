#include "delay_port.h"
#include <crux/delay.h>
#include <stdio.h>

static int failures, waits, rate_reads;
static uint32_t rate_hz, waited_us;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
uint32_t delay_test_clock_hz(void) {
    ++rate_reads;
    return rate_hz;
}
void k_busy_wait(uint32_t usec) {
    ++waits;
    waited_us = usec;
}
int main(void) {
    crux_duration_t out = crux_duration_from_ns(73);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(0)) == CRUX_OK);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(-1)) == CRUX_ERR_INVALID_ARGUMENT);
    CHECK(rate_reads == 0 && waits == 0);
#if EXPECT_SUPPORTED
    /* Each rate represents a different stable configuration, not runtime scaling. */
    const struct {
        uint32_t rate;
        uint32_t max_us;
    } cases[] = {{1, UINT32_MAX},
                 {128, UINT32_MAX},
                 {1000000, 2147483647},
                 {600000000, 3579139},
                 {UINT32_MAX, 499999}};
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        rate_hz = cases[i].rate;
        uint64_t expected_us = cases[i].max_us;
        const int64_t expected_ns = (int64_t)expected_us * 1000;
        CHECK(crux_delay_get_max_duration(&out) == CRUX_OK);
        CHECK(crux_duration_to_ns(out) == expected_ns);
        int before = waits;
        CHECK(crux_delay_busy_wait(crux_duration_from_ns(expected_ns)) == CRUX_OK);
        CHECK(waits == before + 1 && waited_us == expected_us);
        CHECK(crux_delay_busy_wait(crux_duration_from_ns(expected_ns + 1)) ==
              CRUX_ERR_OUT_OF_RANGE);
        CHECK(waits == before + 1);
        CHECK(crux_delay_port_wait_ns(expected_ns + 1) == CRUX_ERR_OUT_OF_RANGE);
        CHECK(crux_delay_port_wait_ns(INT64_MAX) == CRUX_ERR_OUT_OF_RANGE);
        CHECK(waits == before + 1);
        CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_OK && waited_us == 1);
        CHECK(crux_delay_busy_wait(crux_duration_from_ns(1001)) == CRUX_OK && waited_us == 2);
        CHECK(crux_delay_busy_wait(crux_duration_from_ns(INT64_MAX)) == CRUX_ERR_OUT_OF_RANGE);
    }
    rate_hz = 600000000;
    CHECK(crux_delay_get_max_duration(&out) == CRUX_OK && crux_duration_to_ns(out) == 3579139000LL);
    int before = waits;
    /* This native uint32-us input would wrap cycle conversion to104 cycles. */
    CHECK(crux_delay_busy_wait(crux_duration_from_us(7158279)) == CRUX_ERR_OUT_OF_RANGE);
    CHECK(waits == before);
    rate_hz = 0;
    out = crux_duration_from_ns(73);
    CHECK(crux_delay_get_max_duration(&out) == CRUX_ERR_NOT_SUPPORTED &&
          crux_duration_to_ns(out) == 73);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_NOT_SUPPORTED &&
          waits == before);
#else
    rate_hz = 600000000;
    CHECK(crux_delay_get_max_duration(&out) == CRUX_ERR_NOT_SUPPORTED &&
          crux_duration_to_ns(out) == 73);
    CHECK(crux_delay_busy_wait(crux_duration_from_ns(1)) == CRUX_ERR_NOT_SUPPORTED && waits == 0);
    CHECK(crux_delay_port_wait_ns(1) == CRUX_ERR_NOT_SUPPORTED && waits == 0);
    CHECK(rate_reads == 0);
#endif
    printf("Zephyr Delay conversion/profile: %d failures\n", failures);
    return failures != 0;
}
