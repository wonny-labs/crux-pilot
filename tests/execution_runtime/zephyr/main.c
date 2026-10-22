#include "runtime_fixture.h"

#include <zephyr/ztest.h>

ZTEST(execution_runtime, test_bounded_runtime_sessions) {
    int result = crux_runtime_fixture_run();
    zassert_equal(result, 0, "runtime fixture failed at source line %d", result);
}

ZTEST_SUITE(execution_runtime, NULL, NULL, NULL, NULL, NULL);
