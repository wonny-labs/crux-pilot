/* tests/reboot/pending.c on native_sim: the reboot service waiting on the real network settings and
 * configuration saves, with crux_mutex_lock wrapped as on the Host. */
#include "main_common.h"

#include <zephyr/ztest.h>

int reboot_pending_main(void);

ZTEST(reboot_pending, test_run)
{
    zassert_equal(reboot_pending_main(), 0, "a case of pending failed");
    reboot_native_expect_one_case("pending");
}
ZTEST_SUITE(reboot_pending, NULL, NULL, NULL, NULL, NULL);
