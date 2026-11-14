/* What the management route suites share: the real Web core, session module, status service and
 * configuration service on the real Access core, with the fake Access ports, the fake Configuration
 * store, fake status sources, fake session ports and the fake transport. Each case runs in a forked
 * child, so that every case starts the services from nothing. */
#ifndef CRUX_WEB_MANAGEMENT_TEST_H
#define CRUX_WEB_MANAGEMENT_TEST_H

#include "access_test.h"
#include "fake_events.h"
#include "fake_sources.h"
#include "fault_provider.h"
#include "session_test.h"
#include "web_management_internal.h"

#include <stdlib.h>

/* The web core's body store, to show that a request's body does not stay in it. */
bool web_core_holds_bytes(const void *bytes, size_t length);

void mgmt_run_case(const char *name, void (*body)(void));
int mgmt_finish(const char *suite);

/* Every account has this password. */
#define MGMT_PASSWORD SESSION_TEST_PASSWORD

/* The accounts, and what each holds:
 *   admin     access.manage, status.read, app.read, cfg.read, cfg.write, config.save
 *   reader    status.read, cfg.read
 *   writer    cfg.write
 *   both      cfg.read, cfg.write
 *   saver     config.save
 *   app       app.read
 *   stranger  other.thing, which nothing uses
 *   forced    both, with a password change forced
 */

/* The configuration table, in order. The permissions are cfg.read and cfg.write unless noted. */
#define NAME_MODE "mode"          /* BOOL, default false. */
#define NAME_LIMIT "limit"        /* U32, default 5, validator 1 to 100. */
#define NAME_HOST "host_name"     /* STRING, default "crux", rejects text holding "bad". Restart. */
#define NAME_SMALL "small"        /* STRING, default "", capacity 8. */
#define NAME_SECRET "link_secret" /* STRING, sensitive. Restart. */
#define NAME_FIXED "fixed"        /* U32, read-only, default 7. Safe state. */
#define NAME_PRIVATE "private_key" /* U32, under a permission that no role holds. */
#define MGMT_TABLE_ENTRIES 7u

extern crux_config_param_t param_mode, param_limit, param_host, param_small, param_secret,
    param_fixed, param_private;
extern const crux_config_service_entry_t mgmt_table[MGMT_TABLE_ENTRIES];

/* What a case starts. Nothing here is started twice. */
typedef struct {
    bool no_status;                                 /* Leave the status service unstarted. */
    bool no_config;                                 /* Leave the configuration service unstarted. */
    bool no_management;                             /* Leave crux_web_management_init() uncalled. */
    const crux_mgmt_status_config_t *status_config; /* Instead of the default one. */
    const crux_config_service_entry_t *table;       /* Instead of mgmt_table. */
    size_t table_count;
    void (*prepare)(void); /* Initialises the parameters of table, before the service starts. */
} mgmt_options_t;

void mgmt_setup_with(const mgmt_options_t *options);
void mgmt_setup(void);

/* A web login of the account, made once for the case, through the worker step. */
const session_jar_t *mgmt_jar(const char *user);
/* A session of the account made directly, for a call to a service or a check of its state. */
crux_access_session_t mgmt_session(const char *user);

const fake_reply_t *mgmt_get(const session_jar_t *jar, const char *url);
const fake_reply_t *mgmt_put(const session_jar_t *jar, const char *url, const char *body);
const fake_reply_t *mgmt_post(const session_jar_t *jar, const char *url, const char *body);
const fake_reply_t *mgmt_delete(const session_jar_t *jar, const char *url);
/* A request with no session and no token, as a script would send it. Origin and Content-Type are
 * set the way a browser would, so that only the session is missing. */
const fake_reply_t *mgmt_anonymous(unsigned method, const char *url, const char *body);
/* A request of the jar's session with one thing changed. */
typedef struct {
    bool no_token;
    bool no_origin;
    const char *content_type; /* NULL keeps application/json, "" sends none. */
} mgmt_variation_t;
const fake_reply_t *mgmt_vary(const session_jar_t *jar, unsigned method, const char *url,
                              const char *body, mgmt_variation_t variation);

/* The reply is from the core, has this status and holds exactly {"code":"..."}. */
void mgmt_expect_error(const fake_reply_t *reply, int status, const char *code);
/* The reply is from the core with this status and exactly this body. */
void mgmt_expect_body(const fake_reply_t *reply, int status, const char *body);
/* The server's own plain answer. */
void mgmt_expect_plain(const fake_reply_t *reply, int status, const char *text);
/* The security headers, content type and no-store that every answer of the core carries. */
void mgmt_expect_headers(const fake_reply_t *reply, const char *content_type);
/* The reply has a Retry-After of whole seconds. */
void mgmt_expect_retry_after(const fake_reply_t *reply);

/* What the service holds, as the application sees it. */
void mgmt_expect_u32(const char *name, uint32_t expected);
void mgmt_expect_bool(const char *name, bool expected);
void mgmt_expect_string(const char *name, const char *expected);
/* Holds the configuration service's lock from another thread, inside a save of the application's
 * own that has marked a private parameter, until it is released. The save is the application's, so
 * no session is involved, and what the write hook sees is its own. */
void mgmt_hold_service(void);
void mgmt_release_service(void);

/* How many parameters the service marks unsaved, which only an update changes. */
size_t mgmt_unsaved(void);

#endif
