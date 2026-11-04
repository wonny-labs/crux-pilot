/* The suites that join the real crux_access core to the real provider and the real Security event
 * core. Access's fake ports stand in for crypto and storage. */
#ifndef CRUX_SECURITY_EVENT_JOINED_H
#define CRUX_SECURITY_EVENT_JOINED_H

#include "access_test.h"
#include "event_test.h"

/* Each suite links with --wrap=crux_access_port_event, so that the core's calls reach this. It
 * records on Access's fake, which keeps its count of each type, then calls the real provider. */
// NOLINTNEXTLINE(bugprone-reserved-identifier)
void __real_crux_access_port_event(const crux_access_event_t *event);
// NOLINTNEXTLINE(bugprone-reserved-identifier)
void __wrap_crux_access_port_event(const crux_access_event_t *event);
/* The fake's port, renamed for this composition. See CMakeLists.txt. */
void access_fake_port_event(const crux_access_event_t *event);

/* Runs after the provider has emitted, inside the port, with no lock held. */
void joined_port_hook(event_test_hook_t hook, void *context);

/* Boots the event core, then Access with the reference policy and an administrator. */
crux_access_session_t joined_boot_admin(void);

#endif
