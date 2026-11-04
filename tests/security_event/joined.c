#include "joined.h"

static event_test_hook_t port_hook;
static void *port_context;

void joined_port_hook(event_test_hook_t hook, void *context)
{
    port_hook = hook;
    port_context = context;
}

// NOLINTNEXTLINE(bugprone-reserved-identifier)
void __wrap_crux_access_port_event(const crux_access_event_t *event)
{
    access_fake_port_event(event);
    __real_crux_access_port_event(event);
    if (port_hook != NULL) {
        port_hook(port_context);
    }
}

crux_access_session_t joined_boot_admin(void)
{
    event_test_boot();
    joined_port_hook(NULL, NULL);
    access_test_boot_with_admin();
    return access_test_session(ACCESS_TEST_ADMIN, ACCESS_TEST_PASSWORD, &access_test_web);
}
