/* tests/reboot/lock_faults.c on native_sim, with crux_mutex_lock wrapped as on the Host. */
#include "main_common.h"

#include <zephyr/ztest.h>

int reboot_lock_faults_main(void);

ZTEST(reboot_lock_faults, test_run)
{
    zassert_equal(reboot_lock_faults_main(), 0, "a case of lock_faults failed");
    reboot_native_expect_one_case("lock_faults");
}
ZTEST_SUITE(reboot_lock_faults, NULL, NULL, NULL, NULL, NULL);
