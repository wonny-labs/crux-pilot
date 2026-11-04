#include <crux_credential_adapter.h>

#include <crux/credential_store.h>

static_assert(CRUX_CREDENTIAL_OK == 0, "the headers must be usable from C++");
static_assert(CRUX_CREDENTIAL_STATE_UNUSABLE == 0, "uninitialised storage must fail closed");
