#ifndef CRUX_SECURITY_EVENT_ACCESS_CHECK_STUB_H
#define CRUX_SECURITY_EVENT_ACCESS_CHECK_STUB_H

#include <crux/access.h>

/* Sets the answer of crux_access_check, which starts as CRUX_ACCESS_OK. */
void access_check_stub_set(crux_access_result_t result);
unsigned access_check_stub_count(void);
/* True while every check so far named events.read. */
bool access_check_stub_asked_for_read(void);

#endif
