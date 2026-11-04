/* Shared fixture of the status suites: the real Access core with its fake ports, and fake sources.
 */
#ifndef MGMT_STATUS_TEST_H
#define MGMT_STATUS_TEST_H

#include "access_test.h"
#include "fake_sources.h"

#include <crux/management_status.h>

void run_case(const char *name, void (*body)(void));
int suite_finish(const char *suite);

extern crux_access_session_t s_admin;  /* status.read, app.read and a role's manage permission. */
extern crux_access_session_t s_viewer; /* status.read. */
extern crux_access_session_t s_app;    /* app.read only. */
extern crux_access_session_t s_none;   /* A permission that nothing here uses. */
extern const crux_access_session_t s_never_valid;

/* Boots access, logs in each account, and resets the fake sources. Does not start the service. */
void fixture_boot(void);
/* A configuration with no project items, whose reset cause was captured as POR and WATCHDOG. */
crux_mgmt_status_config_t fixture_config(void);
/* The above, then starts the service. */
void fixture_start(void);

/* Walks the list until this name, and returns its entry. A missing name fails the case. */
crux_mgmt_status_entry_t entry_named(crux_access_session_t session, const char *name);
/* Whether this name is in the session's list. */
bool listed(crux_access_session_t session, const char *name);
/* Checks a text item. */
void expect_text_item(crux_access_session_t session, const char *name, const char *expected);
void expect_failed_item(crux_access_session_t session, const char *name, crux_status_t status);
/* Whether a value is all zero, as a failed entry's must be. */
bool value_is_zero(const crux_mgmt_status_value_t *value);

#endif
