// The private interface must also compile for a C++ translation unit.
#include "crux_ssh_provider.h"

static_assert(CRUX_SSH_AUTH_DENIED == 0, "A callback that returns zero must deny");
