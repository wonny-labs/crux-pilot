#include "web_management_network.h"

static_assert(WEB_MGMT_NETWORK_BODY_MAX >= 128u, "the reader's header must be usable from C++");
