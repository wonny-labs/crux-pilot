/* tests/reboot/sleep_provider.c on native_sim, over the real Zephyr Sync provider with the semaphore
 * take wrapped as on the Host. It has one case and needs no isolation. */
#include <zephyr/ztest.h>

int reboot_sleep_provider_main(void);

ZTEST(reboot_sleep_provider, test_run)
{
    zassert_equal(reboot_sleep_provider_main(), 0, "the sleep provider checks failed");
}
ZTEST_SUITE(reboot_sleep_provider, NULL, NULL, NULL, NULL, NULL);
