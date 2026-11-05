/* The clock of crux_access over a stand-in for the Time contract. */
#include "access_port.h"

#include <crux/time.h>

#include <stdio.h>

static crux_status_t answer;
static int64_t now_ns;

crux_status_t crux_monotonic_now(crux_monotonic_time_t *out)
{
    if (answer == CRUX_OK) {
        out->private_ns = now_ns;
    }
    return answer;
}

static unsigned failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    answer = CRUX_OK;
    now_ns = 0;
    CHECK(crux_access_port_now_ms() == 0u);
    now_ns = 999999;
    CHECK(crux_access_port_now_ms() == 0u);
    now_ns = 1000000;
    CHECK(crux_access_port_now_ms() == 1u);
    now_ns = INT64_C(86400000) * INT64_C(1000000);
    CHECK(crux_access_port_now_ms() == UINT64_C(86400000));

    /* A clock that failed reads as one that ran backwards, which expires sessions and never clears
     * a bucket: the fail-closed direction the core documents. A frozen reading would keep them. */
    answer = CRUX_ERR_NOT_READY;
    CHECK(crux_access_port_now_ms() == 0u);
    /* A negative reading cannot wrap into a far future. */
    answer = CRUX_OK;
    now_ns = -1;
    CHECK(crux_access_port_now_ms() == 0u);

    printf("access clock provider: %u failures\n", failures);
    return failures == 0 ? 0 : 1;
}
