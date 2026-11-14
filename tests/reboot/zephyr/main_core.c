/* The core suites of tests/reboot on native_sim: each Host main() is a function here. */
#include "main_common.h"

#include <zephyr/ztest.h>

int reboot_service_main(void);
int reboot_steps_main(void);
int reboot_concurrency_main(void);
int reboot_reason_main(void);

#define REBOOT_SUITE(name)                                                                         \
    ZTEST(reboot_##name, test_run)                                                                 \
    {                                                                                              \
        zassert_equal(reboot_##name##_main(), 0, "a case of " #name " failed");                    \
        reboot_native_expect_one_case(#name);                                                      \
    }                                                                                              \
    ZTEST_SUITE(reboot_##name, NULL, NULL, NULL, NULL, NULL)

REBOOT_SUITE(service);
REBOOT_SUITE(steps);
REBOOT_SUITE(concurrency);
REBOOT_SUITE(reason);
