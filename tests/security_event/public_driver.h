/* The suite in public_cases.c runs against the real core and against the fake, using the public
 * header and these two controls only. */
#ifndef CRUX_SECURITY_EVENT_PUBLIC_DRIVER_H
#define CRUX_SECURITY_EVENT_PUBLIC_DRIVER_H

#include <crux/security_event.h>

/* Returns the implementation to the state before init. */
void public_driver_reset(void);
/* Sets what the read path's permission check answers. */
void public_driver_access_answer(crux_access_result_t answer);

#endif
