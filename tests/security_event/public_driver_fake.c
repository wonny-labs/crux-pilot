#include "public_driver.h"
#include "security_event_fake.h"

void public_driver_reset(void)
{
    security_event_fake_reset();
}

void public_driver_access_answer(crux_access_result_t answer)
{
    security_event_fake_access_answer(answer);
}
