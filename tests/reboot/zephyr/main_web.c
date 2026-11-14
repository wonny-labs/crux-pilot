/* tests/web/reboot/routes.c and openapi_fixture.c on native_sim, with the route table that names
 * the reboot routes. crux_web_respond and crux_mutex_lock are wrapped as on the Host. */
#include "main_common.h"

#include <zephyr/ztest.h>

int reboot_routes_main(void);
int reboot_openapi_fixture_main(void);

#define REBOOT_SUITE(name)                                                                         \
    ZTEST(reboot_##name, test_run)                                                                 \
    {                                                                                              \
        zassert_equal(reboot_##name##_main(), 0, "a case of " #name " failed");                    \
        reboot_native_expect_one_case(#name);                                                      \
    }                                                                                              \
    ZTEST_SUITE(reboot_##name, NULL, NULL, NULL, NULL, NULL)

REBOOT_SUITE(routes);
REBOOT_SUITE(openapi_fixture);
