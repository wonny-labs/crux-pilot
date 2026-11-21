/* A start that is refused, and a retry. On Crux Sync the service created a mutex and a semaphore at
 * the start, and a start that failed on the second had to give the first back, or each retry would
 * use up a Sync slot for good (the fault of #513). The Zephyr-direct service (pilot of ADR-0002 P9)
 * defines its lock, its signal and its worker statically, so a start creates nothing and cannot
 * fail for want of an object. What is left to hold is the invariant behind that fix: a refused
 * start leaves no service and no state that a retry would meet, any number of refused starts cost
 * nothing, and the start that follows them works. */
#include "reboot_test.h"

static void refused_starts_leave_nothing_and_a_later_start_works(void)
{
    crux_reboot_config_t refused = reboot_config();
    crux_reboot_state_t state;

    reboot_boot();
    refused.policy = NULL; /* A callback kind with no callback states no policy. */

    for (unsigned attempt = 0; attempt < 100u; ++attempt) {
        CHECK(crux_reboot_start(&refused) == CRUX_REBOOT_ERR_NO_POLICY);
        CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_ERR_NOT_READY);
        CHECK(crux_reboot_start_worker() == CRUX_REBOOT_ERR_NOT_READY);
    }

    /* The start that follows works, and a second one changes nothing. */
    const crux_reboot_config_t config = reboot_config();
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_start(&config) == CRUX_REBOOT_OK);
    CHECK(crux_reboot_get(s_admin, &state) == CRUX_REBOOT_OK);
}

int main(void)
{
    reboot_run_case("refused starts leave nothing and a later start works",
                    refused_starts_leave_nothing_and_a_later_start_works);
    return reboot_finish("reboot_start_faults");
}
