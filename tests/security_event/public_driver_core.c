#include "access_check_stub.h"
#include "event_test.h"
#include "public_driver.h"

static bool drain_may_run;

void public_driver_reset(void)
{
    if (drain_may_run) {
        /* The suite may have started the drain, which refuses a stop if it has not. */
        (void)crux_security_event_test_stop_drain();
    }
    drain_may_run = true;
    event_test_reset();
    /* The cases never depend on a clock, and the drain's thread uses this one to sleep. */
    event_test_clock_real(true);
    access_check_stub_set(CRUX_ACCESS_OK);
}

void public_driver_access_answer(crux_access_result_t answer)
{
    access_check_stub_set(answer);
}
