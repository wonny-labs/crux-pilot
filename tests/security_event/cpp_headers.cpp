#include <crux/security_event.h>

static_assert(CRUX_SECURITY_EVENT_OK == 0, "the header must be usable from C++");
static_assert(CRUX_SECURITY_EVENT_ID_ACCESS_LOGIN_OK == 0x0100, "the identifiers are constants");
