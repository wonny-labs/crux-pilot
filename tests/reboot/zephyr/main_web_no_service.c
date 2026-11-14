/* tests/web/reboot/no_service.c on native_sim: the route table has no reboot route. */
#include "main_common.h"

#include <zephyr/ztest.h>

int reboot_no_service_main(void);

ZTEST(reboot_no_service, test_run)
{
    zassert_equal(reboot_no_service_main(), 0, "a case of no_service failed");
    reboot_native_expect_one_case("no_service");
}
ZTEST_SUITE(reboot_no_service, NULL, NULL, NULL, NULL, NULL);
