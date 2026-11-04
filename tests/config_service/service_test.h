/* Shared fixture of the configuration service suites: the real Access core with its fake ports, the
 * fake Configuration provider, and a recording event port. */
#ifndef CONFIG_SERVICE_TEST_H
#define CONFIG_SERVICE_TEST_H

#include "access_test.h"
#include "config_service_port.h"
#include "fake_events.h"
#include "fault_provider.h"

#include <crux/config_service.h>

/* Run a case in a forked child, so that each starts the service from nothing. */
void run_case(const char *name, void (*body)(void));
/* The suite's exit status. */
int suite_finish(const char *suite);

/* One account of each kind, logged in. A parameter below uses cfg.read and cfg.write. */
extern crux_access_session_t s_admin;    /* Both, and config.save. */
extern crux_access_session_t s_reader;   /* cfg.read. */
extern crux_access_session_t s_writer;   /* cfg.write. */
extern crux_access_session_t s_both;     /* cfg.read and cfg.write. */
extern crux_access_session_t s_saver;    /* config.save only. */
extern crux_access_session_t s_stranger; /* A permission that nothing here uses. */

/* The table, in order. */
#define NAME_MODE "mode"           /* BOOL, default false. */
#define NAME_LIMIT "limit"         /* U32, default 5, validator 1 to 100. */
#define NAME_WIDE "wide"           /* U32, default 0, no validator. */
#define NAME_HOST "host_name"      /* STRING, default "crux", rejects text holding "bad". */
#define NAME_SMALL "small"         /* STRING, default "", capacity 8. */
#define NAME_SECRET "link_secret"  /* STRING, sensitive. Restart activation. */
#define NAME_FIXED "fixed"         /* U32, read-only, default 7. Safe state activation. */
#define NAME_PRIVATE "private_key" /* U32, under a permission no role holds. */
#define TABLE_ENTRIES 8u

extern crux_config_param_t param_mode, param_limit, param_wide, param_host, param_small,
    param_secret, param_fixed, param_private;
extern const crux_config_service_entry_t test_table[TABLE_ENTRIES];

/* Boots access, logs in each account, and initialises each parameter. Does not start the service.
 */
void fixture_boot(void);
/* The above, then starts the service on the table. */
void fixture_start(void);

/* Log in again, after an operation that ended the account's sessions. */
crux_access_session_t fixture_login(const char *user);

/* A session that holds no slot. */
extern const crux_access_session_t s_never_valid;

/* Denials that crux_access_check recorded since the last clear. */
size_t denials(void);
/* A session's expected view of a table parameter, through read. */
void expect_text(crux_access_session_t session, const char *name, const char *expected);
/* The accepted value, seen as the application sees it. */
void expect_u32(const char *name, uint32_t expected);
void expect_string(const char *name, const char *expected);
crux_config_service_result_t update_text(crux_access_session_t session, const char *name,
                                         const char *text);

#endif
